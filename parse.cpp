#include "gumbo.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <map>
#include <set>

#include <fstream>
#include <sstream>
#include <iostream>

#include <climits>
#include <cassert>

#include <cstring>
#include <cmath>

#include <random>

#include "ansi-colors.h"
#include "color.h"

GumboNode* find_tag(GumboNode *node, GumboTag targetTag) {
    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboElement *element = &node->v.element;

        if (element->tag == targetTag)
            return node;

        GumboVector* children = &element->children;

        for (int i = 0; i < children->length; ++ i)
            if (GumboNode* found_in_child = find_tag((GumboNode*) children->data[i], targetTag))
                return found_in_child;
    }

    return nullptr;
}

struct annotated_symbol {
    char symbol;
    int64_t classes;
};

struct highlighted_text {
    std::vector<annotated_symbol> annotated_symbols;

    std::string css_styles;

    std::map<std::set<std::string>, unsigned> class_ids;
    std::map<unsigned, std::set<std::string>> class_names;

    std::map<std::set<std::string>, std::string> highlights_combined;
    // combines       ^~~~~~~~~~~ many highlights int one
    std::set<std::string> highlights;
};

std::string join_to_string(const auto &strings) {
    std::string result;

    bool is_first = true;
    for (const auto &string: strings) {
        if (!is_first)
            result += " ";
        is_first = false;

        result += string;
    }

    return result;
}

std::string escape_in_html(char symbol) {
    std::string output;

    switch (symbol) {
    case '<':
        output += "&lt";
        break;

    case '>':
        output += "&gt";
        break;

    case '&':
        output += "&amp";
        break;

    default:
        output += symbol;
        break;
    }

    return output;
}

unsigned get_or_add_class(highlighted_text &text, const std::set<std::string>& classes) {
    unsigned id = 0;
    if (text.class_ids.contains(classes))
        id = text.class_ids[classes];
    else {
        id = text.class_ids[classes] = text.class_ids.size() + 1;
        text.class_names[id] = classes;
    }

    return id;
}

void collect_all_text(GumboNode *node, highlighted_text &text,
                      std::set<std::string>& inherited_classes) {

    switch (node->type) {
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_WHITESPACE: {
        unsigned id = get_or_add_class(text, inherited_classes);

        for (const char* current = node->v.text.text; *current != '\0'; ++ current)
            text.annotated_symbols.push_back({ *current, id });

        break;
    }

    case GUMBO_NODE_CDATA:
    case GUMBO_NODE_DOCUMENT:
    case GUMBO_NODE_COMMENT:
    case GUMBO_NODE_TEMPLATE:
        // No useful for us information in this nodes...

        break;

    case GUMBO_NODE_ELEMENT:
        GumboElement* element = &node->v.element;

        std::set<std::string> new_classes = inherited_classes;

        GumboVector* attributes = &element->attributes;
        for (int i = 0; i < attributes->length; ++ i) {
            GumboAttribute *attribute = (GumboAttribute*) attributes->data[i];

            // Only look at "class" attributes:
            if (std::strcmp(attribute->name, "class") != 0)
                continue;


            new_classes.insert(attribute->value);
            //                 ^~~~~~~~~~~~~~~~ what if multiple classes?

            // Determine id of current class:
            unsigned id = get_or_add_class(text, new_classes);

            // Only so much space at our disposal in int64_t, and I
            // really don't see any point in extending it for current
            // purposes, 64 should be plenty.
            assert(id < sizeof(int64_t) * CHAR_BIT && "Too many classes!");
        }

        GumboVector* children = &element->children;

        for (int i = 0; i < children->length; ++ i)
            collect_all_text((GumboNode*) children->data[i], text, new_classes);

        break;
    }
}

std::string read_whole_file(const char *filename) {
    std::ifstream input_file { filename };
    std::stringstream ss;

    ss << input_file.rdbuf();
    return ss.str();
}

std::string extract_style(GumboNode *node) {
    GumboNode *style_node = find_tag(node, GUMBO_TAG_STYLE);
    assert(style_node && "No styles provided for text");

    GumboVector *nodes = &style_node->v.element.children;
    assert(nodes->length == 1 && "<style> should only contain text");

    GumboNode* css_styles_text_node = (GumboNode*) nodes->data[0];
    assert(css_styles_text_node->type == GUMBO_NODE_TEXT
           && "<style> should only contain text");

    std::string css_styles = css_styles_text_node->v.text.text;
    return css_styles;
}

highlighted_text extract_highlighted_text(const char* filename) {
    std::string text = read_whole_file(filename);
    GumboOutput* output = gumbo_parse(text.c_str());

    GumboNode *inner_pre = find_tag(output->root, GUMBO_TAG_PRE);
    assert(inner_pre && "Highlighted file should be wrapped in a single <pre>");

    highlighted_text highlighted {};
    highlighted.css_styles = extract_style(output->root);

    std::set<std::string> classes;
    collect_all_text(inner_pre, highlighted, classes);

    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return highlighted;
}

void print_debug(highlighted_text &text) {
    for (auto symbol: text.annotated_symbols) {
        int i = 23 /* COLOR_RED */ + symbol.classes - 1;
        if (i == 30) ++ i;

        std::cout << "\033[" << i << "m";
        std::cout << symbol.symbol << COLOR_RESET;
    }
}

std::string read_stdin() {
    std::string total;

    std::string current_line;
    while (std::getline(std::cin, current_line)) {
        total += current_line;
        total += '\n';
    }

    return total;
}

std::string html_reconstruct(const highlighted_text &text) {
    std::string body;

    bool has_open_span = false;

    int64_t current = 0;
    for (auto symbol: text.annotated_symbols) {
        if (symbol.classes != current) {
            // Close previous span:
            if (has_open_span)
                body += "</span>";

            has_open_span = false;

            const std::set<std::string>& classes = text.class_names.at(symbol.classes);

            if (symbol.classes) { // Symbol has designated classes
                body += "<span class=\"" + join_to_string(classes) + "\">";
                has_open_span = true;
            }
        }

        body += escape_in_html(symbol.symbol);

        current = symbol.classes;
    }


    std::stringstream ss;
    ss << "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n";

    ss << "<html>"                                 "\n"
          "    <head>"                             "\n"
          "        <title> Test </title>"          "\n"
          "        <style type=\"text/css\">"      "\n"
       <<              text.css_styles             <<
          "        </style>"                       "\n"
          "    </head>"                            "\n"
          "    <body>"                             "\n"
          "        <pre>"                          "\n"
       <<                   body                   <<
          "        </pre>"                         "\n"
          "    </body>"                            "\n"
          "</html>";

    return ss.str();
}



struct location { int line, column; };

struct target_region {
    location from, to;
};

std::vector<target_region> parse_target_regions(std::string input) {
    std::istringstream iss(input);

    std::vector<target_region> regions;

    target_region target;
    while (iss >> target.from.line >> target.from.column
               >> target.to  .line >> target.to  .column) {

        if (target.from.line == 0 || target.to.line == 0)
            continue;

        regions.push_back(target);
    }
           

    return regions;
}

int find_index(const highlighted_text &text, location loc) {
    int index = 0;
    for (int line = 1; line < loc.line; ++ line) {
        // Got to the end of the line:
        while (text.annotated_symbols[index].symbol != '\n')
            ++ index;

        ++ index; // Skip \n in the end of the line
    }

    return index + (loc.column - 1);
}

std::string new_highlight(hsv &base, highlighted_text &text) {
    std::stringstream new_class_name_builder;
    new_class_name_builder << "highlight" << std::rand();

    std::string highlight_name = new_class_name_builder.str();

    base.h = ((int) base.h + rand() % 80 + 120) % 360; // Rotate hue by arbitrary amount

    std::stringstream new_css_style;
    new_css_style << "." + highlight_name + " {"                                  "\n";
    new_css_style << "    background-color: " << rgb_to_string(hsv2rgb(base)) << ";\n";
    new_css_style << "}"                                                          "\n";

    text.css_styles += new_css_style.str();

    text.highlights.insert(highlight_name);

    return highlight_name;
}

void highlight(highlighted_text &text, const std::vector<target_region>& regions) {
    hsv base = { 285.0, 28.7 / 100.0, 38.0 / 100.0 }; // Base color!

    int index = 0;
    for (auto region: regions) {

        // std::cerr << "Progress: " << (index ++ / (double) regions.size()) << "\r";

        std::string highlight_name = new_highlight(base, text);

        for (int i = find_index(text, region.from); i <= find_index(text, region.to); ++ i) {
            if (i == 0) {
                std::cerr << "!" << text.annotated_symbols[i].symbol;
                std::cerr << "\n";;
            }

            annotated_symbol &current = text.annotated_symbols[i];

            std::set<std::string> classes = text.class_names[current.classes];

            // Find are there highlights?
            std::set<std::string> intersect;
            set_intersection(classes.begin(), classes.end(),
                             text.highlights.begin(), text.highlights.end(),
                             std::inserter(intersect, intersect.begin()));

            intersect.insert(highlight_name); // Add new highlight

            if (intersect.size() == 0)
                classes.insert(highlight_name);
            else {
                classes.erase(*intersect.begin());
                //            ^~~~~~~~~~~~~~~~~~ assuming one element in set

                if (text.highlights_combined.contains(intersect))
                    classes.insert(text.highlights_combined[intersect]);
                else {
                    std::string combined = new_highlight(base, text);
                    text.highlights_combined[intersect] = combined;
                }
            }
            current.classes = get_or_add_class(text, classes);
        }

    }

}

int main(int argc, char *argv[]) {
    std::srand(0);

    const char *filename = argv[1];
    highlighted_text text = extract_highlighted_text(filename);

    highlight(text, parse_target_regions(read_stdin()));
    std::cout << html_reconstruct(text);
}
