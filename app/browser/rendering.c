#include "rendering.h"

#include "tokenize.h"
#include "parse.h"
#include "lib.h"

// TODO: improve this hacky way.
bool in_list = 0;

void markdown(Node *node) {
  switch (node->element_type) {
    case TEXT:
      write(1, node->data, strlen(node->data));
      break;
    case HEADING:
      if (strcmp(node->local_name, "h1") == 0) {
        write(1, "# ", 2);
      } else if (strcmp(node->local_name, "h2") == 0) {
        write(1, "## ", 3);
      } else if (strcmp(node->local_name, "h3") == 0) {
        write(1, "### ", 4);
      } else if (strcmp(node->local_name, "h4") == 0) {
        write(1, "#### ", 5);
      } else if (strcmp(node->local_name, "h5") == 0) {
        write(1, "##### ", 6);
      } else if (strcmp(node->local_name, "h6") == 0) {
        write(1, "###### ", 7);
      }
      break;
    case LI:
      if (in_list)
        write(1, "\n", 1);
      write(1, "- ", 2);
      in_list = 1;
      break;
    default:
      break;
  }
}

void generate() {
  Node *node = root_node;

  while (node) {
    markdown(node);

    Node *next = node->next;
    while (next) {
      markdown(next);
      next = next->next;
    }

    node = node->first_child;
  }
}

void render(char* html) {
  tokenize(html);
  //print_tokens();

  //println("================");

  construct_tree();
  //println("================");
  //print_nodes();

  //println("================");

  generate();
  //write(1, "\n", 1);
}
