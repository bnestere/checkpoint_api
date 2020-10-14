#ifdef __cplusplus
extern "C" {
#endif

typedef struct _restore_context {
  int restore_successful; // 0 if not successful, 1 if is

  int n_ids; // Number of identified regions
  unsigned long *nvm_offset_vals_by_id; // starting at 0, the location that an identified datum will exist in the backup
  void **dram_ptrs_by_id; // points to the application-used location in memory for an identified datum
  int *region_size_by_id; // sizes of each region 

  int n_dram_ptrs; // maintain all pointers to data by a unique id
  void **cp_pointer_offset_from_by_ptr_id; // Offset of a pointer variable
  void **cp_pointer_offset_to_by_ptr_id; // Offset to where the pointer variable points 

} restore_context_t;

typedef struct _backup_context {

  int n_root_vars; // Number of root variables
  int id_ctr; // Keeps track of the next ID to assign
  unsigned long region_ptr; // Points to the next free data size
  unsigned long *nvm_offset_vals_by_id; // starting at 0, the location that an identified datum will exist
  void **dram_ptrs_by_id; // points to the application-used location in memory for an identified datum
  int *region_size_by_id; // sizes of each region 

  int dram_ptr_id; // maintain all pointers to data by a unique id
  void **dram_pointer_addr_by_dram_id; // dram address of a pointer
  void **dram_pointer_to_by_dram_id; // pointed to ID of a DRAM pointer

} backup_context_t;

typedef struct _thread_cp_context {
  int thread_id;
  int n_phases;
  int cur_phase;
  int *phase_cur_iter;
  int *phase_end_iter;

  /*
   * Whether or not this thread is being reloaded
   *  0 if not reloaded (fresh start), 1 if is reloaded
   */
  int is_reloaded; 
} thread_cp_context_t;

backup_context_t *init_backup_default(int n_root_vars);
backup_context_t *init_backup(int n_ids, int n_root_vars);

/*
 * Registers the existence of a pointer.
 */
void register_pointer_ref(backup_context_t *bc, void *from_addr, void *to_addr);

/*
 * Checkpoints a custom variable at the root level
 */
void checkpoint_custom_root(backup_context_t *bc, void *base, int size, void (*cp_func)(backup_context_t *bc, void *dram_loc), int id);

/*
 * Checkpoints a custom data structure with a dynamic id
 */
void checkpoint_custom(backup_context_t *bc, void *base, int size, void (*cp_func)(backup_context_t *bc, void *dram_loc));

/*
 * Checkpints an array with a preset array at the root level
 */
void checkpoint_array_root(backup_context_t *bc, void *base, int el_size, int n_els, void (*el_backup_func)(backup_context_t *bc, int arr_idx, void *el_ptr), int id);

/*
 *
 */
void checkpoint_array(backup_context_t *bc, void *base, int el_size, int n_els, void (*el_backup_func)(backup_context_t *bc, int arr_idx, void *el_ptr));

void do_backup(backup_context_t *bc);
void increment_thread_backup(thread_cp_context_t *tcp);

restore_context_t *try_restore(void); 

#ifdef __cplusplus
}
#endif
