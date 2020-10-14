#include <cp_api.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _node node_t;

struct _node {
  node_t *next;
  int value;
};

void node_backup(backup_context_t *bc, void *ws_loc) {
  node_t *node = (node_t *) ws_loc, *tmp;

  tmp = node;
  if(tmp != NULL && tmp->next != NULL) {
    printf("Backing up node with value %d\n", node->value);
    register_pointer_ref(bc, &(tmp->next), tmp->next);

    checkpoint_custom(bc, tmp->next, sizeof(node_t), node_backup);
  }
}

void head_backup(backup_context_t *bc, int arr_idx, void *head_loc) {
  node_backup(bc, head_loc);
}


int main(int argc, char *argv[]) {
  int i, size = 5;
  node_t *head,*tmp;

  restore_context_t *rc;
  rc = try_restore();

  if(rc->restore_successful) {
    printf("Restoring existing backup\n");
    head = rc->dram_ptrs_by_id[0];
  } else {
    printf("No backup to restore, creating new\n");
    head = (node_t *) malloc(sizeof(node_t));
    head->value = 0;
    node_t *tail = head;

    for(i=1; i <= size; i++) {
      tmp = (node_t *) malloc(sizeof(node_t));
      tmp->value = i;
      tail->next = tmp;
      tail = tmp;
    }

    backup_context_t *bc = init_backup_default(1);
    checkpoint_array_root(bc, head, sizeof(node_t), 1, head_backup, 0);
    do_backup(bc);
  }

  tmp = head;
  while(tmp != NULL) {
    printf("Node with value: %d\n", tmp->value);
    tmp = tmp->next;
  }
  
  return 0;
}
