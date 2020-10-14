#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <string.h>

#include "cp_api.h"

#define DEFAULT_MAP_SIZE 4194304
//#define DEFAULT_MAP_SIZE 5
#define DEFAULT_POINTER_MAP_SIZE 4194304

backup_context_t *init_backup(int n_ids, int n_root_vars) {
  unsigned long *tmp_ul_ptr;
  backup_context_t *backup_ctx = malloc(sizeof(backup_context_t));
  backup_ctx->region_ptr = sizeof(int); // reserve 0 for null ptrs
  backup_ctx->id_ctr = n_root_vars; // reserve (n_root_vars - 1) ids for root vars
  backup_ctx->n_root_vars = n_root_vars;
  backup_ctx->dram_ptr_id = 0;

  if(backup_ctx == NULL) {
    printf("Failed to alloc backup_ctx\n");
    exit(1);
  }

  backup_ctx->nvm_offset_vals_by_id = calloc(n_ids, sizeof(unsigned long));
  backup_ctx->dram_ptrs_by_id = calloc(n_ids, sizeof(void *));
  if(backup_ctx->dram_ptrs_by_id == NULL) {
    printf("Failed to allocate dram_ptrs_by_id\n");
    exit(1);
  }

  backup_ctx->region_size_by_id = calloc(n_ids, sizeof(int));

  backup_ctx->dram_pointer_addr_by_dram_id = malloc(sizeof(void*) * DEFAULT_POINTER_MAP_SIZE);
  backup_ctx->dram_pointer_to_by_dram_id = malloc(sizeof(void*) * DEFAULT_POINTER_MAP_SIZE);
  return backup_ctx;
}

backup_context_t *init_backup_default(int n_root_vars) {
  return init_backup(DEFAULT_MAP_SIZE, n_root_vars);
}

void destroy_backup(backup_context_t *bc) {

  if(bc == NULL) {
    return;
  }

  free(bc->nvm_offset_vals_by_id);
  free(bc->dram_ptrs_by_id);
  free(bc->region_size_by_id);
  free(bc->dram_pointer_addr_by_dram_id);
  free(bc->dram_pointer_to_by_dram_id);
  free(bc);
}

void register_pointer_ref(backup_context_t *bc, void *from_addr,  void *points_to) {
  int my_id = bc->dram_ptr_id;
  if(points_to == NULL) {
    // Ignore NULL pointers because they are essentially literals and can be copied as such
    printf("register_pointer_ref: Ignoring because points_to is NULL\n");
    return;
  }

  bc->dram_pointer_addr_by_dram_id[my_id] = from_addr;
  bc->dram_pointer_to_by_dram_id[my_id] = points_to;
  my_id = bc->dram_ptr_id++;
  //printf("Registered pointer for my_id %d from %p to %p\n", my_id, from_addr, points_to);
}

int addr_already_registered(backup_context_t *bc, void *addr) {
  int i;
  int is_registered = 0;
  for(i = 0; i < bc->id_ctr; i++) {
    void *region_base = bc->dram_ptrs_by_id[i];
    int region_size = bc->region_size_by_id[i];
    void *end_region = (char*) region_base + region_size;

    if(region_base <= addr && addr < end_region) {
      is_registered = 1;
      break;
    }
  }
  
  return is_registered;
}


void checkpoint_custom_id_set(backup_context_t *bc, void *base, int size, void (*cp_func)(backup_context_t *bc, void *dram_loc), int my_id) {

  if(addr_already_registered(bc,base)) {
    printf("Addr %p is already registered, skipping\n", base);
    return;
  }
  unsigned long nvm_offset = bc->region_ptr;

  bc->nvm_offset_vals_by_id[my_id] = nvm_offset;

  bc->dram_ptrs_by_id[my_id] = base;
  bc->region_size_by_id[my_id] = size;
  
  //printf("Updated region_size_by_id index %d", my_id);
  bc->region_ptr += size;

  void *nvm_base = base;

  cp_func(bc, nvm_base);
}

void checkpoint_custom_root(backup_context_t *bc, void *base, int size, void (*cp_func)(backup_context_t *bc, void *dram_loc), int root_id) {
  checkpoint_custom_id_set(bc, base, size, cp_func, root_id);
}

void checkpoint_custom(backup_context_t *bc, void *base, int size, void (*cp_func)(backup_context_t *bc, void *dram_loc)) {
  int new_id = bc->id_ctr++;
  checkpoint_custom_id_set(bc, base, size, cp_func, new_id);
}

void checkpoint_array_id_set(backup_context_t *bc, void *base, int el_size, int n_els, void (*el_backup_func)(backup_context_t *bc, int arr_idx, void *el_ptr), int my_id) {

  int i;

  unsigned long base_ptr = bc->region_ptr;
  //int my_id = bc->id_ctr++;
  
  //if(el_size < 8) el_size = 8;

  bc->nvm_offset_vals_by_id[my_id] = base_ptr;
  bc->dram_ptrs_by_id[my_id] = base;
  bc->region_ptr += n_els * el_size;
  bc->region_size_by_id[my_id] = n_els * el_size;

  /*
   * Do we need additional post processing?
   */
  if(el_backup_func != NULL) {
    // yes, call their post processing function
    for(i = 0; i < n_els; i++) {
      void *arr_ele = (char *) base + (el_size * i);
      el_backup_func(bc, i, arr_ele);
    }
  }
}

void checkpoint_array_root(backup_context_t *bc, void *base, int el_size, int n_els, void (*el_backup_func)(backup_context_t *bc, int arr_idx, void *el_ptr), int root_id) {
  checkpoint_array_id_set(bc, base, el_size, n_els, el_backup_func, root_id);
}

void checkpoint_array(backup_context_t *bc, void *base, int el_size, int n_els, void (*el_backup_func)(backup_context_t *bc, int arr_idx, void *el_ptr)) {
  int new_id = bc->id_ctr++;
  checkpoint_array_id_set(bc, base, el_size, n_els, el_backup_func, new_id);
}

void save_descriptor_file(backup_context_t *bc) {
  char descriptor_filename[64] = "/mnt/pmem/descriptor.desc_cp.tmp";
  
  long descriptor_areasize = sizeof(unsigned long) /*highest id*/
    + sizeof(unsigned long) /*number of pointers in map*/
    //+ sizeof(unsigned long) /*max region size*/
    + sizeof(unsigned long) * bc->id_ctr /*region_size_by_id*/;

  int descriptor_fd = open(descriptor_filename, O_RDWR | O_CREAT | O_TRUNC, (mode_t) 0600);
  if(descriptor_fd == -1) {
    printf("Failed to create %s\n",descriptor_filename);
    exit(-1);
  }
  if(lseek(descriptor_fd, descriptor_areasize, SEEK_SET) == -1) {
    close(descriptor_fd);
    perror("Error calling lseek() to 'stretch' the file");
    exit(-1);
  }
  if (write(descriptor_fd, "", 1) == -1)
  {
    close(descriptor_fd);
    perror("Error writing last byte of the file");
    exit(-1);
  }

  unsigned long *descriptor_map = (unsigned long *) mmap(0, descriptor_areasize, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_fd, 0);
  if (descriptor_map == MAP_FAILED)
  {
    close(descriptor_fd);
    perror("Error mmapping the descriptor file");
    exit(-1);
  }

  unsigned long n_ids = bc->id_ctr; 
  unsigned long n_ptrs = bc->dram_ptr_id;
  unsigned long max_region = bc->region_ptr;

  descriptor_map[0] = n_ids;
  descriptor_map[1] = n_ptrs;

  int *region_sizes_base = (int *) &descriptor_map[2];
  int i;
  for(i = 0; i < n_ids; i++) {
    int region_size = bc->region_size_by_id[i];
    region_sizes_base[i] = region_size;
  }

  msync(descriptor_map, descriptor_areasize, MS_SYNC);
  munmap(descriptor_map, descriptor_areasize);
  close(descriptor_fd);
  
}

void try_restore_descriptor_file(restore_context_t *rc) {
  char descriptor_filename[64] = "/mnt/pmem/descriptor.desc_cp";
  
  if( access(descriptor_filename, F_OK) == -1 ) {
    // File doesn't exist, stop trying to restore
    printf("File %s doesn't exist\n", descriptor_filename);
    rc->restore_successful = 0;
    return;
  }

  int fd = open(descriptor_filename, O_RDWR, (mode_t) 0600);
  if(fd == -1) {
    printf("Failed to create %s\n", descriptor_filename);
    exit(-1);
  }
  long areasize = lseek(fd, 0, SEEK_END) + 1;

  unsigned long *area_map = (unsigned long *) mmap(0, areasize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (area_map == MAP_FAILED)
  {
    close(fd);
    perror("Error mmapping the descriptor file");
    exit(-1);
  }

  unsigned long n_ids = area_map[0];
  unsigned long n_ptrs = area_map[1];


  rc->n_ids = (int) n_ids;
  rc->n_dram_ptrs = (int) n_ptrs;

  /*
   * Initialize restoration variable fields
   */
  rc->nvm_offset_vals_by_id = calloc(n_ids, sizeof(unsigned long));
  rc->dram_ptrs_by_id = calloc(n_ids, sizeof(void *));
  rc->region_size_by_id = calloc(n_ids, sizeof(int));

  rc->cp_pointer_offset_from_by_ptr_id = calloc(n_ptrs, sizeof(void*));
  rc->cp_pointer_offset_to_by_ptr_id = calloc(n_ptrs, sizeof(void*));

  int *region_sizes_base = (int *) &area_map[2];
  int i;
  unsigned long tmp_offset = 0;
  for(i = 0; i < n_ids; i++) {
    int region_size = region_sizes_base[i];
    void *region_dram_base = malloc(region_size);

    if(region_dram_base == NULL) {
      fprintf(stderr, "Failed to restore checkpointed region; please delete checkpoint files and restart anew");
      exit(1);
    }
    
    rc->nvm_offset_vals_by_id[i] = tmp_offset;
    rc->dram_ptrs_by_id[i] = region_dram_base;
    rc->region_size_by_id[i] = region_size;
    tmp_offset += region_size;
  }

  msync(area_map, areasize, MS_SYNC);
  munmap(area_map, areasize);
  close(fd);
}

void save_data_file(backup_context_t *bc) {
  char data_filename[64] = "/mnt/pmem/data.dat_cp.tmp";
  
  long data_areasize = bc->region_ptr /* Already in bytes */;

  int data_fd = open(data_filename, O_RDWR | O_CREAT | O_TRUNC, (mode_t) 0600);
  if(data_fd == -1) {
    printf("Failed to create %s\n",data_filename);
    exit(-1);
  }
  if(lseek(data_fd, data_areasize, SEEK_SET) == -1) {
    close(data_fd);
    perror("Error calling lseek() to 'stretch' the file");
    exit(-1);
  }
  if (write(data_fd, "", 1) == -1)
  {
    close(data_fd);
    perror("Error writing last byte of the file");
    exit(-1);
  }

  char *data_map = (char *) mmap(0, data_areasize, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0);
  if (data_map == MAP_FAILED)
  {
    close(data_fd);
    perror("Error mmapping the data file");
    exit(-1);
  }

  int i;
  unsigned long cur_offset = 0;
  for(i = 0; i < bc->id_ctr; i++) {
    char *dest = &data_map[cur_offset];
    char *source = bc->dram_ptrs_by_id[i];
    int size = bc->region_size_by_id[i];
    memcpy(dest, source, size);
    cur_offset += size;
  }

  msync(data_map, data_areasize, MS_SYNC);
  munmap(data_map, data_areasize);
  close(data_fd);
}

void try_restore_data(restore_context_t *rc) {
  char data_filename[64] = "/mnt/pmem/data.dat_cp";
  
  if( access(data_filename, F_OK) == -1 ) {
    // File doesn't exist, stop trying to restore
    printf("File %s doesn't exist\n", data_filename);
    rc->restore_successful = 0;
    return;
  }

  int fd = open(data_filename, O_RDWR, (mode_t) 0600);
  if(fd == -1) {
    printf("Failed to create %s\n", data_filename);
    exit(-1);
  }
  long areasize = lseek(fd, 0, SEEK_END) + 1;

  unsigned long *area_map = (unsigned long *) mmap(0, areasize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (area_map == MAP_FAILED)
  {
    close(fd);
    perror("Error mmapping the data file");
    exit(-1);
  }
  int i;
  for(i = 0; i < rc->n_ids; i++) {
    unsigned long nvm_offset = rc->nvm_offset_vals_by_id[i] / sizeof(unsigned long);
    
    void *source = (void *) &area_map[nvm_offset];
    void *dest = rc->dram_ptrs_by_id[i];
    int region_size = rc->region_size_by_id[i];
    memcpy(dest, source, region_size);

  }

  msync(area_map, areasize, MS_SYNC);
  munmap(area_map, areasize);
  close(fd);
}

void save_ptrs_file(backup_context_t *bc) {

  if(bc->dram_ptr_id == 0) {
    // No pointers to save, don't do anything
    return;
  }

  char ptrs_filename[64] = "/mnt/pmem/ptrs.ptr_cp.tmp";

  long ptrs_areasize = (sizeof(void *) * bc->dram_ptr_id) * 2; // *2 for two sets of pointers in the map

  int ptrs_fd = open(ptrs_filename, O_RDWR | O_CREAT | O_TRUNC, (mode_t) 0600);
  if(ptrs_fd == -1) {
    printf("Failed to create %s\n",ptrs_filename);
    exit(-1);
  }
  if(lseek(ptrs_fd, ptrs_areasize, SEEK_SET) == -1) {
    close(ptrs_fd);
    perror("Error calling lseek() to 'stretch' the file");
    exit(-1);
  }
  if (write(ptrs_fd, "", 1) == -1)
  {
    close(ptrs_fd);
    perror("Error writing last byte of the file");
    exit(-1);
  }

  void **ptrs_map = (void **) mmap(0, ptrs_areasize, PROT_READ | PROT_WRITE, MAP_SHARED, ptrs_fd, 0);
  if (ptrs_map == MAP_FAILED)
  {
    close(ptrs_fd);
    perror("Error mmapping the ptrs file");
    exit(-1);
  }


  //printf("special pointer vals %p to %p\n", bc->dram_pointer_addr_by_dram_id[1],bc->dram_pointer_to_by_dram_id[1]);



  int i,j;
  for(i = 0; i < bc->dram_ptr_id; i ++) {
    
    // First get actual dram pointer value
    void *dram_from_addr = bc->dram_pointer_addr_by_dram_id[i];
    void *dram_to_addr = bc->dram_pointer_to_by_dram_id[i];

    void *offset_from_addr = NULL, *offset_to_addr = NULL;

    // We need to find the offsets, if we don't, these stay 0 and we error and exit
    int offset_from_found = 0;
    int offset_to_found = 0;

    // Convert these to NVM offsets
    unsigned long tmp_offset = 0;
    for(j = 0; j < bc->id_ctr; j++) {
      int region_size = bc->region_size_by_id[j];
      char *dram_begin_region = (char *) bc->dram_ptrs_by_id[j];
      char *dram_end_region = (char *) dram_begin_region + region_size;

      //printf("Dram region %d from %p to %p\n", j, dram_begin_region, dram_end_region);

      unsigned long begin_reg = tmp_offset;
      //unsigned long end_reg = begin_reg = region_size;
      
      if(dram_begin_region <= dram_from_addr && dram_from_addr < dram_end_region) {
        unsigned long region_data_offset = (unsigned long) dram_from_addr - (unsigned long) dram_begin_region; 
        char *nvm_area_offset = region_data_offset + begin_reg;
        offset_from_addr = (void *) nvm_area_offset;
        offset_from_found = 1;
      }

      if(dram_begin_region <= dram_to_addr && dram_to_addr < dram_end_region) {
        unsigned long region_data_offset = (char *) dram_to_addr - dram_begin_region; 
        char *nvm_area_offset = region_data_offset + begin_reg;
        offset_to_addr = (void *) nvm_area_offset;
        offset_to_found = 1;
      }

      tmp_offset += region_size;
    }

    if(!offset_from_found) {
      fprintf(stderr, "Failed to backup ptr id=%d, offset_from_addr is NULL.\ndram_from_addr: %p\ndram_to_addr is %p\n", 
          i, 
          dram_from_addr,
          dram_to_addr);
      exit(1);
    }
    if(!offset_to_found) {
      fprintf(stderr, "Failed to backup ptr id=%d, offset_to_addr is NULL.\ndram_from_addr: %p\ndram_to_addr is %p\n", 
          i, 
          dram_from_addr,
          dram_to_addr);
      exit(1);
    }

    //printf("Saving id %d offset from %p to %p (dram %p to %p)\n", i, offset_from_addr, offset_to_addr, dram_from_addr, dram_to_addr);

    ptrs_map[i*2] = offset_from_addr;
    ptrs_map[(i*2)+1] = offset_to_addr;
  }

  msync(ptrs_map, ptrs_areasize, MS_SYNC);
  munmap(ptrs_map, ptrs_areasize);
  close(ptrs_fd);
}

void try_restore_ptrs(restore_context_t *rc) {
  char ptrs_filename[64] = "/mnt/pmem/ptrs.ptr_cp";
  
  if( access(ptrs_filename, F_OK) == -1 ) {
    // File doesn't exist, stop trying to restore
    printf("File %s doesn't exist\n", ptrs_filename);
    rc->restore_successful = 0;
    //TODO Free fields in restore context because the restore failed
    return;
  }

  int fd = open(ptrs_filename, O_RDWR, (mode_t) 0600);
  if(fd == -1) {
    printf("Failed to create %s\n", ptrs_filename);
    exit(-1);
  }
  long areasize = lseek(fd, 0, SEEK_END) + 1;

  void **area_map = (void **) mmap(0, areasize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (area_map == MAP_FAILED)
  {
    close(fd);
    perror("Error mmapping the ptrs file");
    exit(-1);
  }
  int i, j;
  for(i = 0; i < rc->n_dram_ptrs; i++) {
    void *cp_offset_from = area_map[i*2];
    void *cp_offset_to = area_map[(i*2)+1];
    rc->cp_pointer_offset_from_by_ptr_id[i] = cp_offset_from;
    rc->cp_pointer_offset_to_by_ptr_id[i] = cp_offset_to;
    

    void *dram_addr_from, *dram_addr_to;
    int found_from = 0, found_to = 0;
    
    //printf("Begin restore id %d cp addr from offset %p to %p\n", i, cp_offset_from, cp_offset_to);

    /*
     * Find the DRAM addrs for each NVM checkpoint offset
     */
    unsigned long tmp_offset = 0;
    for(j = 0; j < rc->n_ids; j++) {
      int region_size = rc->region_size_by_id[j];
      char *nvm_begin_region = (char *) rc->nvm_offset_vals_by_id[j];
      char *nvm_end_region = (char *) nvm_begin_region + region_size;
      char *dram_begin_region = (char *) rc->dram_ptrs_by_id[j];

      //printf("Examining offset region id %d: %p to %p (dram %p to %p)\n", j,
      //    nvm_begin_region, nvm_end_region, rc->dram_ptrs_by_id[j], dram_begin_region + region_size);
      
      if(nvm_begin_region <= cp_offset_from && cp_offset_from < nvm_end_region) {
        unsigned long region_data_offset = (char *) cp_offset_from - nvm_begin_region; 
        char *dram_data_addr = region_data_offset + dram_begin_region;
        found_from = 1;

        dram_addr_from = (void *) dram_data_addr;
      }
      if(nvm_begin_region <= cp_offset_to && cp_offset_to < nvm_end_region) {

        unsigned long region_data_offset = (char *) cp_offset_to - nvm_begin_region; 
        char *dram_data_addr = region_data_offset + dram_begin_region;

        found_to = 1;
        //printf("Found to\n");

        dram_addr_to = (void *) dram_data_addr;
      }

      tmp_offset += region_size;
    }

    if(!found_from) {
      fprintf(stderr, "Failed to restore ptr, found_from is NULL. cp_offset_from is %p\n", cp_offset_from);
      exit(1);
    }
    if(!found_to) {
      fprintf(stderr, "Failed to restore ptr, found_to is NULL. cp_offset_to is %p\n", cp_offset_to);
      exit(1);
    }

    //printf("Restoring id %d dram addr %p to %p (offset %p to %p)\n", i, dram_addr_from, dram_addr_to, cp_offset_from, cp_offset_to);
    void **dram_from_ptr = dram_addr_from;
    *dram_from_ptr = dram_addr_to;
    
    //*dram_addr_from = dram_addr_to;
  }

  msync(area_map, areasize, MS_SYNC);
  munmap(area_map, areasize);
  close(fd);
}

restore_context_t *try_restore() {
  restore_context_t *rc = malloc(sizeof(restore_context_t));
  rc->restore_successful = 1; // Assume success until proven otherwise
  try_restore_descriptor_file(rc);
  if(rc->restore_successful == 0) { 
    // Something messed up, just return
    return rc;
  }
  try_restore_data(rc);
  if(rc->restore_successful == 0) { 
    // Something messed up, just return
    return rc;
  }
  if(rc->n_dram_ptrs > 0) {
    // Only restore pointers if they were saved
    try_restore_ptrs(rc);
  }
  return rc;
}

void do_backup(backup_context_t *bc) {
  int tmp_id;
  int i;

  save_descriptor_file(bc);
  save_data_file(bc);
  save_ptrs_file(bc);

  char descriptor_filename_from[64] = "/mnt/pmem/descriptor.desc_cp.tmp";
  char descriptor_filename_to[64] = "/mnt/pmem/descriptor.desc_cp";
  char data_filename_from[64] = "/mnt/pmem/data.dat_cp.tmp";
  char data_filename_to[64] = "/mnt/pmem/data.dat_cp";
  char ptrs_filename_from[64] = "/mnt/pmem/ptrs.ptr_cp.tmp";
  char ptrs_filename_to[64] = "/mnt/pmem/ptrs.ptr_cp";

  rename(descriptor_filename_from, descriptor_filename_to);
  rename(data_filename_from, data_filename_to);
  rename(ptrs_filename_from, ptrs_filename_to);

  destroy_backup(bc);
}
