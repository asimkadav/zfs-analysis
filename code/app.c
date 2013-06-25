/* User space component of the gray box error injection technique.
 * We read the on-disk information here to make the driver more
 * intelligent and to perform the type aware corruption.
 */

/* System header files. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h> 
#include <sys/open.h>

/* Our header files     */
#include "app.h"

//#define BLK_LYR_DEVICE "/devices/pseudo/zfs_lyr@1:zfsminor1"
#define BLK_LYR_DEVICE "/dev/ramdisk/zfs_layer"
#define CHR_LYR_DEVICE "/devices/pseudo/zfs_lyr@1:zfsminor2" 

vdev_label_t vdev_lab[4];
objset_phys_t mos, comp_mos;
dnode_phys_t *mos_dp;
// FILE *fp;
static int fp;
static int fp2;

corrupt_t *mct = NULL; //master corruption table is NULL initially 

/* Forward Declarations */
//void populate_blkptr_info (blkptr_t, int *, int *, int *);
int populate_blkptr_info (blkptr_t bp, char **data, int *size, int which_dva);
void print_micro_zap(char *microzap_obj);
uint64_t microzap_getvalue(char *microzap_obj, char *name, int num_entries);
void print_dnode_info(dnode_phys_t * dn);
int traverse_dir(dnode_phys_t *,int, int, char **);
void traverse_dataset(char * which_dataset, dnode_phys_t * os_data, int datasetnum);
void traverse_objset(char * which_objset, blkptr_t bp, dnode_phys_t *os_data[], char ** objset_phy);
void recursive_populate(blkptr_t * bp, int nlevel, char *dn_array[], int *size, int dn_sizes[], char*); 
void populate_corruption_table(char *,uint64_t,uint32_t,uint32_t);  
int set_corruption(char *,char); 
void traverse_mct(void);
void print_zil_contents(objset_phys_t * os);

/* Populates the corruption table with the required corruption
 * entry.
 */
void populate_corruption_table(char * name, uint64_t new_value, uint32_t blockno, uint32_t offset)
{
  corrupt_t *new_cte = (corrupt_t *) malloc (sizeof(corrupt_t));
  new_cte->ptr_name = (char *) malloc (strlen(name)+1);
  strcpy(new_cte->ptr_name, name);
  new_cte->new_value = new_value; 
  new_cte->blockno = blockno;
  new_cte->bit = 0;
  new_cte->offset = 2; //offset; 
  new_cte->nxt = NULL;

  if (mct == NULL)
    mct = new_cte;
  else  
  { 
   corrupt_t * temp = mct;
   while (temp->nxt != NULL)
      temp = (corrupt_t *)temp->nxt; 
    temp->nxt = (struct corrupt_t * ) new_cte; 
  } 
}

int set_corruption(char * name, char bit)
{
  corrupt_t * temp = mct;
  while (temp !=NULL)
  {
    if (temp->ptr_name == NULL)
      return 1;
    else
      if (strcmp(temp->ptr_name,name) == 0)
      {  temp->bit = bit; return 0;} 
    temp = (corrupt_t *) temp->nxt; 
  }

}

int get_blockno(char *name)
{
  corrupt_t * temp = mct;
  while (temp !=NULL)
  {
    if (temp->ptr_name == NULL)
    {
      printf("No entry for \"%s\" in corruption table\n", name); 
      return -1;
    }
    else
      if (strcmp(temp->ptr_name,name) == 0)
      {  
        return temp->blockno;
      }
    temp = (corrupt_t *) temp->nxt;
  }
}

int drv_send(int *offset)
{
  corrupt_t * temp = mct;
  while (temp !=NULL)
  {
    if (temp->bit == 1) 
    {
        //ioctl(fp2, 3393, temp);  
        *offset = temp->offset;
        return temp->blockno;
    }
    temp = (corrupt_t *) temp->nxt; 
  }
}


void traverse_mct(void)
{
  corrupt_t * temp = mct;
  printf ("Corruption Table as follows.\n"); 
  if (temp == NULL)
    printf ("Master corruption table is empty.\n"); 
  else
    while (temp !=NULL) 
    {
      printf ("------------------------------\n");
      printf ("|N: %s| NV: %x|BN: %d|BIT: %d|OFFSET: %d|\n",temp->ptr_name,temp->new_value,temp->blockno, temp->bit, temp->offset);    
      printf ("------------------------------\n");
      temp = (corrupt_t *)temp->nxt; 
    } 
    printf ("\n\nN: name, NV: New value(after corruption)\n BN: Block number, BIT: Corruption bit (1=corruption)\n"); 

}
/*
void record_physblk(char *name, int blkno)
{
 */ 
 

/* The lzjb decompression algorithm to uncompress the phys_t
 * structures.
 */
int
lzjb_decompress (void *s_start, void *d_start, size_t s_len, size_t d_len,
		 int n)
{
  uchar_t *src = s_start;
  uchar_t *dst = d_start;
  uchar_t *d_end = (uchar_t *) d_start + d_len;
  uchar_t *cpy, copymap;
  int copymask = 1 << (NBBY - 1);

  while (dst < d_end)
    {
      if ((copymask <<= 1) == (1 << NBBY))
	{
	  copymask = 1;
	  copymap = *src++;
	}
      if (copymap & copymask)
	{
	  int mlen = (src[0] >> (NBBY - MATCH_BITS)) + MATCH_MIN;
	  int offset = ((src[0] << NBBY) | src[1]) & OFFSET_MASK;
	  src += 2;
	  if ((cpy = dst - offset) < (uchar_t *) d_start)
	    return (-1);
	  while (--mlen >= 0 && dst < d_end)
	    *dst++ = *cpy++;
	}
      else
	{
	  *dst++ = *src++;
	}
    }
  return (0);
}



void
print_fsinfo ()
{
  int i, j, ibest, jbest;
  struct uberblock *ub;
  struct uberblock *ubbest;
  time_t timestamp;
  uint64_t mos_pos;
  printf ("Vdev label information.\n");

  ubbest = (struct uberblock *) malloc (sizeof (struct uberblock));
  ub = (struct uberblock *) malloc (sizeof (struct uberblock));
  ubbest->ub_txg = 0;

  /* We traverse the uberblocks on all four vdev labels of our ramdisk
   * and find the uberblock with the maximum ub_txg and store it in ubbest. 
   */
  for (j = 0; j < 4; j++)
    // we assume that there are 128 uberblocks in each label 
    for (i = 0; i < 128; i++)
      {
	ub = (struct uberblock *) &vdev_lab[j].vl_uberblock[1024 * i];
	timestamp = ub->ub_timestamp;
	if (ub->ub_magic == UBERBLOCK_MAGIC)	// get only non-zero uber-blocks. 
	  {
	    if (ub->ub_txg > ubbest->ub_txg)
	    {   
              *ubbest = *ub;
	       ibest = i; 
               jbest = j;
            } 
           /* 
            printf ("label %d uber %d | ", j, i);
	    printf ("magic = %llx | ", (u_longlong_t) ub->ub_magic);
	    printf ("version = %llu | ", (u_longlong_t) ub->ub_version);
	    printf ("txg = %llu | ", (u_longlong_t) ub->ub_txg);
	    printf ("guid_sum = %llx | ", (u_longlong_t) ub->ub_guid_sum);
	    printf ("UTC = %s | ",
		    asctime (localtime (&timestamp)));
	    printf ("rootbp->vdev(with asize) = %llu  offset  = %llu\n",
		    ub->ub_rootbp.blk_dva[0].dva_word[0],
		    ub->ub_rootbp.blk_dva[0].dva_word[1] << 1ULL);
           */ 
	  }
      }
  
  // Start: uberblock data corruption 
  int ubbest_phys_offset;
  printf("ibest= %d, jbest= %d\n", ibest, jbest);
  if (jbest<2)
    ubbest_phys_offset = 256*1024*jbest + 128*1024 + 1024*ibest; 
  else if(jbest==2)
    ubbest_phys_offset = (64*1048576 - 256*1024*2) + 128*1024 + 1024*ibest; 
  else if(jbest==3)
    ubbest_phys_offset = (64*1048576 - 256*1024) + 128*1024 + 1024*ibest; 
  else
    printf("Error in finding uberbest!!!\n");

    
  //int offset 
  populate_corruption_table("uberdata", 0, ubbest_phys_offset/512, 16);  
  // End: uberblock data corruption 
  

  printf ("Printing the active uberblock: \n");
  timestamp = ubbest->ub_timestamp;
  printf ("\tmagic = %016llx\n", (u_longlong_t) ubbest->ub_magic);
  printf ("\tversion = %llu\n", (u_longlong_t) ubbest->ub_version);
  printf ("\ttxg = %llu\n", (u_longlong_t) ubbest->ub_txg);
  printf ("\tguid_sum = %llu\n", (u_longlong_t) ubbest->ub_guid_sum);
  printf ("\ttimestamp = %llu UTC = %s", (u_longlong_t) ubbest->ub_timestamp,
	  asctime (localtime (&timestamp)));
  printf ("\trootbp->vdev = %llu  offset  = %llu",
	  ubbest->ub_rootbp.blk_dva[0].dva_word[0],
	  ubbest->ub_rootbp.blk_dva[0].dva_word[1] << 1ULL);
  printf ("Compression = %d\n", (ubbest->ub_rootbp.blk_prop << 24) >> 56);
  printf ("Type = %d\n", (ubbest->ub_rootbp.blk_prop << 8) >> 56);
  //printf (" 
  printf ("\n");

  printf ("\n--------------MOS Details--------------\n");
  dnode_phys_t *mos_data[3];
  objset_phys_t *objset_phy;
  traverse_objset("mos", ubbest->ub_rootbp, mos_data, (char **)&objset_phy);
  printf ("mos data type %d \n", mos_data[0][1].dn_type);
  printf ("\n--------------Object directory Details--------------\n");
  int size;
  char *od_data;
  populate_blkptr_info (mos_data[0][1].dn_blkptr[0], &od_data, &size,0);
  // remember : when to free od_data

  /* Object directory contents are zap objects.
   */

  printf ("\n-------------Object Directory ZAP details -----------\n");
  // we know that it should contain 4 entries 
  print_micro_zap(od_data);
  uint64_t config_dataset = microzap_getvalue(od_data,"config",4);  
  uint64_t root_dataset = microzap_getvalue(od_data,"root_dataset",4);
  printf("Confirm root_dataset = %d\n", root_dataset);
  /* hack: we know by trial and error that root_dataset dir has contents in the third blkptr
     we pass the blkptr number to check as a parameter
  */ 
  printf("\n-------------- Root DSL directory------------\n");
  char *cdzap_data;
  int active_ds_num; 
  active_ds_num = traverse_dir(mos_data[0], root_dataset, 2, &cdzap_data); 
  // remember : when to free cdzap_data

  /* Prints the child_dir_zap_object of root DSL directory */
  uint64_t DMOS_dir = microzap_getvalue(cdzap_data, "myfs", 2);
  printf("\n------------- myfs DSL directory------------\n");
  free(cdzap_data); 
  int myfs_act_ds_num =  traverse_dir(mos_data[0], DMOS_dir, 2, &cdzap_data);  

 /* Retrieve the myfs dataset of root dsl directory*/
 printf("\n-------------- Active dataset Details---------------\n");
 traverse_dataset("myfs dataset", mos_data[0], myfs_act_ds_num);  

 printf("---------------Active Dataset Objset---------------- \n");
 /*Travelling towards DMU objset which turns out to be metaslab information */ 
 dsl_dataset_phys_t *ds = (dsl_dataset_phys_t *)mos_data[0][myfs_act_ds_num].dn_bonus; 
 dnode_phys_t *os_data[3]; 
 
 // Calulating the previous snapshot 
 uint64_t prevsnap=0; 
 dsl_dataset_phys_t *snapds;
 prevsnap = ds->ds_prev_snap_obj;
 
 // reusing objset_phy here
 /* Traverse the objset inside this dataset */ 
 traverse_objset("$myfs-os", ds->ds_bp, os_data, (char **)&objset_phy); 
 //traverse_objset("$myfs-os", snapds->ds_bp, os_data, (char **)&objset_phy); 
 
 printf("Active Dataset Objset ZIL Info---------- \n");
 // traversing the ZFS Intent Log
 print_zil_contents(objset_phy); 
 int phys_block;
 char *zil_data; int zil_data_size;
 phys_block = populate_blkptr_info(objset_phy->os_zil_header.zh_log, &zil_data, &zil_data_size, 0); 
 populate_corruption_table("zil_start", 2, phys_block, 0); 

 // read the zil_data by interpreting it as zil records 
 lr_t * zil_record;
 zil_record = (lr_t *) zil_data;
 printf("lrc_txtype = %llu  ", zil_record->lrc_txtype);
 printf("lrc_reclen = %llu  ", zil_record->lrc_reclen);
 printf("lrc_txg = %llu  ", zil_record->lrc_txg);
 printf("lrc_seq = %llu\n", zil_record->lrc_seq);
 
 /* myfs_os has indirect pointers, so we know that 
  * what we get is an array of blkptr_t 
 */
 printf("Indirect Objects (blkptrs)-----------\n"); 
 blkptr_t *bp = (blkptr_t *) os_data[0]; 
 char * dn_array[50];int dn_size = 0; int dn_sizes[50]; 
 recursive_populate(bp, 6, dn_array, &dn_size, dn_sizes, "myfs-os"); 
 int q,r;

 for (q=0;q<dn_size;q++) 
   for(r=0;r<dn_sizes[q]/sizeof(dnode_phys_t);r++)  
     print_dnode_info ( ((dnode_phys_t *)dn_array[q]) + r); 

  /* Master node is at index(object number) 1 in ZPL dataset.
   * It is a ZAP object.
   */ 
   dnode_phys_t * dn_myfs_master_node =  ((dnode_phys_t *)dn_array[0]) + 1; 

   // reusing phys_block here
   char * myfs_mn_data;int myfs_mn_data_size; //mn = master node 
   phys_block = populate_blkptr_info(*(dn_myfs_master_node->dn_blkptr), &myfs_mn_data, &myfs_mn_data_size, 0); 
   print_micro_zap(myfs_mn_data);
   populate_corruption_table("master node", 0, phys_block, 0);
   
   int myfs_root_dir_num = microzap_getvalue(myfs_mn_data, "ROOT", 4); 
   dnode_phys_t * dn_root_dir = ((dnode_phys_t *)dn_array[0]) + myfs_root_dir_num;
   char * myfs_root_data;int myfs_root_data_size;  
   phys_block = populate_blkptr_info(*(dn_root_dir->dn_blkptr), &myfs_root_data, &myfs_root_data_size, 0);
   print_micro_zap(myfs_root_data); 
   populate_corruption_table("rootdirzap", 0, phys_block, 0);
      
   int myfs_dir_num = microzap_getvalue(myfs_root_data, "dir", 4); 
   dnode_phys_t * dn_dir = ((dnode_phys_t *)dn_array[0]) + myfs_dir_num;
   char * myfs_dir_data;int myfs_dir_data_size;  
   phys_block = populate_blkptr_info(*(dn_dir->dn_blkptr), &myfs_dir_data, &myfs_dir_data_size, 1);
   print_micro_zap(myfs_dir_data); 
   populate_corruption_table("dirzap", 0, phys_block, 0);
   
   long long int myfs_file_num = microzap_getvalue(myfs_dir_data, "a", 4);
   // hack : values of zap objects in non-root file system objects have MSB set. 
   
   myfs_file_num = (myfs_file_num <<1ULL) >> 1ULL; 
   dnode_phys_t * dn_file_dir = ((dnode_phys_t *)dn_array[0]) + myfs_file_num;
   char * myfs_file_data; int myfs_file_data_size;
   phys_block = populate_blkptr_info(*(dn_file_dir->dn_blkptr), &myfs_file_data, &myfs_file_data_size, 0); 
   populate_corruption_table("filedata", 0, phys_block, 0);
  
   //Printing file contents - yo! 
   int yo;
   for (yo=0;yo<myfs_file_data_size;yo++) 
     printf("%c",myfs_file_data[yo]);

   // Corruping blkptr to file contents
   int file_blkptr_offset;
   int myfs_os_blkno = get_blockno("myfs-osdata0");
   printf("Confirm myfs-osdata0 block number = %d\n", myfs_os_blkno); 
   file_blkptr_offset = myfs_os_blkno*512 + ( (char*)&dn_file_dir->dn_blkptr[0].blk_dva[0].dva_word[1] - (char*)dn_array[0] );
   printf("Confirm fileptr corruption details: block number = %d, offset = %d\n", file_blkptr_offset/512, file_blkptr_offset%512 ); 
   populate_corruption_table("fileptr", 0, file_blkptr_offset/512, file_blkptr_offset%512);

 /* 
  // Trying to find the snapshot object in myfs objset
  // prevsnap is keeping the object number
  printf("------------myfs Snapshot dataset details----------\n"); 
  printf("Snapshot object index = %d\n", prevsnap);
  printf("Treating it as dnode, output:\n");
  print_dnode_info( &(( (dnode_phys_t *)dn_array[0])[prevsnap]) );
  snapds = (dsl_dataset_phys_t *) ((( (dnode_phys_t *)dn_array[0])[prevsnap]).dn_bonus);

  // reusing objset_phy here
  // Traverse the objset inside this dataset  
  dnode_phys_t *snap_os_data[3]; 
  traverse_objset("$myfs-os_snap", snapds->ds_bp, snap_os_data, (char **)&objset_phy); 
 */
}

void recursive_populate(blkptr_t * bp, int nlevel, char * dn_array[], int *size, int dn_sizes[], char *which_objset) 
{
 printf("Calling for nlevel %d  ",nlevel); 
 if ( nlevel == 0)
   return; //If you are here, then this project is in trouble. 
 else
 { 
   int q; 
   for (q=0;q<10;q++)
   { 
      if ((bp->blk_dva[0].dva_word[1]) != 0)
      {
         blkptr_t * bp_child;
         int bp_child_size;int phys_block;
         char name[30]; 
         if(strcmp(which_objset, "myfs-os") == 0)
           phys_block = populate_blkptr_info(*bp, (char **) &bp_child, &bp_child_size, 1); 
         else
           phys_block = populate_blkptr_info(*bp, (char **) &bp_child, &bp_child_size, 0); 


         // Check if block level is 1, then we have the data with us   
         if ( ((bp->blk_prop << 1) >> 57) == 0)
         {
           if (*size < 50)
           {  
              dn_array[*size] = (char *) bp_child;
              dn_sizes[*size] = bp_child_size; 
              sprintf(name, "%sdata%d", which_objset, *size);
              populate_corruption_table(name,0,phys_block, 0);
	      *size = *size + 1;
           } 
           else
               printf("dn_array size exceeded.\n");
           //return;  //Terminating condition 
         } 
         recursive_populate(bp_child, nlevel - 1, dn_array, size, dn_sizes, which_objset); 
         bp++; 
      }  
      else
        break; 
        
    }
 }
}

void traverse_objset(char * which_objset, blkptr_t bp, dnode_phys_t *os_data[], char **objset_phy)
{
  /* we now find the Object Set 
   * mos_pos holds the offet of MOS in terms of 512 byte blocks
   */
  int size, i; 
  char *data;
  objset_phys_t *os; 
  dnode_phys_t * os_dn;
  int phys_block;
  // hack:  
  if (strcmp(which_objset, "$myfs-os") == 0)
  {    
    phys_block = populate_blkptr_info(bp, &data, &size, 0);
  } 
  else
    phys_block = populate_blkptr_info(bp, &data, &size, 0);

  populate_corruption_table(which_objset,0,phys_block, 32);

  // remember : when to free this data ? 
  os = (objset_phys_t *) data;
  // return this objset_phys_t object
  *objset_phy = data; 
  printf ("OS os_type = %llu\n", os->os_type);
  printf ("OS dnode_phys_t = %llu\n", os->os_meta_dnode);
  os_dn = &os->os_meta_dnode;
  print_dnode_info(&os->os_meta_dnode);
  
  /* Reading the block pointers of the dnode pointer. */

  printf ("--- Intra OS Blockpointer details --- \n");

  /* We go in reverse so that we get psize, lsize and mos_pos
   * in the last iteration to get values mos_dp->dn_blkptr[0]
   * in these variables.
   * remember : in each iteration we free data
   */
  char name[30];
  strcpy(name, which_objset);
  
  for (i = os_dn->dn_nblkptr - 1; i >= 0; i--)
  {
    sprintf(name, "%sdata%d", which_objset, i);
    //name[strlen(which_objset)+5]=0;
    if (strcmp(which_objset, "mosdata0") == 0)
      phys_block = populate_blkptr_info (os_dn->dn_blkptr[i],(char **) &os_data[i], &size, 0);
    else  
      phys_block = populate_blkptr_info (os_dn->dn_blkptr[i],(char **) &os_data[i], &size, 0);
    populate_corruption_table(name, 0, phys_block, 0); 
  }
}

/* traverse the directory and print all its child directory contents */
int traverse_dir(dnode_phys_t * os_data, int dirnum, int blkptrnum, char ** zap_data)
{
  time_t timestamp;
  // we print the given  dataset directory contents
  int dsl_dir_size;
  char * dsl_dir_data;
  printf("Directory blkptr------------\n");
  // HACK: Dont know why we got the data in the third blkptr
  // HACK: All DSL dir objects have data in third blkptr 
  populate_blkptr_info (os_data[dirnum].dn_blkptr[blkptrnum], &dsl_dir_data, &dsl_dir_size, 0);
  print_dnode_info(&os_data[dirnum]); 
 
  // DSL dir objects contain a dsl_dir_phys_t structure in the bonus buffer, we get it
  dsl_dir_phys_t *dsl_dir_bonus;
  dsl_dir_bonus = (dsl_dir_phys_t *) os_data[dirnum].dn_bonus;
  timestamp = dsl_dir_bonus->dd_creation_time;
  printf("DSL dir creation time = %s\n",asctime (localtime (&timestamp)));
  printf("DSL dir active dataset object = %llu\n",dsl_dir_bonus->dd_head_dataset_obj);
 
  int active_obj = dsl_dir_bonus->dd_head_dataset_obj;
  //traverse_dataset(os_data, active_obj);
  
  int child_dir = dsl_dir_bonus->dd_child_dir_zapobj;
  int size;
  printf("Child dir blkptr details------------\n");
  // Remember that for ZAP object blkptr[0] is used, no hack here
  populate_blkptr_info (os_data[child_dir].dn_blkptr[0], zap_data, &size, 0);
  // remember : where to free data

  printf ("Child Directory ZAP details -----------\n");
  print_micro_zap(*zap_data);
  return active_obj; 
}


void traverse_dataset(char * which_dataset, dnode_phys_t * os_data, int datasetnum)
{
  printf("Dataset blkptr details------------\n");
  int ds_size;
  char * ds_data;
  int phys_block;
  // Remember that for dataset object blkptr[0] is used, no hack here
  phys_block = populate_blkptr_info (os_data[datasetnum].dn_blkptr[0], &ds_data, &ds_size, 0);
  populate_corruption_table(which_dataset, 0, phys_block, 0); 
  dsl_dataset_phys_t * dataset = (dsl_dataset_phys_t *) os_data[datasetnum].dn_bonus;
  print_dnode_info(&os_data[datasetnum]);
}

/* print dnode_phys_t information */
void print_dnode_info(dnode_phys_t * dn)
{ 
  printf("dn_compress = %d,  ", dn->dn_compress); 
  printf("dn_nblkptr = %d,  ", dn->dn_nblkptr); 
  printf("dn_nlevels = %d,  ", dn->dn_nlevels); 
  printf("dn_type = %d,  ", dn->dn_type); 
  printf("dn_bonustype = %d\n", dn->dn_bonustype); 
  //printf("dn_blkptr[0].blk_dva[0].dva_word[1] = %llu and value at 72 = %llu\n", dn->dn_blkptr[0].blk_dva[0].dva_word[1], *(uint64_t *)((char *)dn + 72) );  
}

/* Print micro ZAP object information */

void print_micro_zap(char *microzap_obj)
{
  int i=0;
  int max_entries = 2046;
  mzap_phys_t *zap_hdr = ((mzap_phys_t *) microzap_obj);
  printf ("zap header block type %llu \n", zap_hdr->mz_block_type);
  printf ("ZBT_MICRO = %llu\n", (1ULL << 63) + 3);
  printf ("ZBT_HEADER = %llu\n", (1ULL << 63) + 1);
  printf ("ZBT_LEAF = %llu\n", (1ULL << 63) + 0);

  // zap object contains name value pairs, we print them here
  printf ("mze_name[0] = %s, mze_value[0] = %llu\n",
	  zap_hdr->mz_chunk[0].mze_name,
	  zap_hdr->mz_chunk[0].mze_value);
  mzap_ent_phys_t *zap_entries = (mzap_ent_phys_t *)(microzap_obj + sizeof(mzap_phys_t)); 

  for(i=0;i<max_entries-1;i++)
  {
    if(strcmp(zap_entries[i].mze_name, "") == 0)
      break;
    printf("mze_name[%d] = %s, mze_value[%d] = %llu\n", i+1, zap_entries[i].mze_name, i+1, zap_entries[i].mze_value);
  } 
}

void print_zil_contents(objset_phys_t * os)
{
  printf("zil_claim_txg = %llu  ", os->os_zil_header.zh_claim_txg); 
  printf("zil_replay_seq = %llu  ", os->os_zil_header.zh_replay_seq); 
  //printf("zil_zh_log = %llu  ", (uint64_t)os->os_zil_header.zh_log); 
  printf("zil_claim_seq = %llu\n", os->os_zil_header.zh_claim_seq); 
  char *zil_data; 
  int size=0;
}


uint64_t microzap_getvalue(char *microzap_obj, char *name, int num_entries)
{
  int i=0;
  mzap_phys_t *zap_hdr = ((mzap_phys_t *) microzap_obj);
  mzap_ent_phys_t *zap_entries = (mzap_ent_phys_t *)(microzap_obj + sizeof(mzap_phys_t)); 
  if(strcmp(zap_hdr->mz_chunk[0].mze_name, name) == 0)
    return zap_hdr->mz_chunk[0].mze_value;
  else
  {
    for(i=0;i<num_entries-1;i++)
      if (strcmp(zap_entries[i].mze_name, name) == 0)
        return zap_entries[i].mze_value;
  }
  if(i == num_entries-1)
    printf("Entry for name = %s not found in ZAP object!!\n", name);
  return 0;
}



/* Given a block pointer, this routine populates the psize, lsize and prints
 * other debug information. 
 */
int populate_blkptr_info (blkptr_t bp, char **data, int *size, int which_dva)
{
  int psize, lsize, pos;
  printf ("bp->vdev1 = %llu  offset1 and gang = %llu,  ",
	  bp.blk_dva[0].dva_word[0]>>32, bp.blk_dva[0].dva_word[1]);
  printf ("bp->vdev2 = %llu  offset2 and gang = %llu,  ",
	  bp.blk_dva[1].dva_word[0]>>32, bp.blk_dva[1].dva_word[1]);
  printf ("bp->vdev3 = %llu  offset3 and gang = %llu,  ",
	  bp.blk_dva[0].dva_word[2]>>32, bp.blk_dva[2].dva_word[1]);
  printf ("Compression = %d,  ", (bp.blk_prop << 24) >> 56);
  printf ("Type = %d,  ", (bp.blk_prop << 8) >> 56);
  printf ("Fill = %llu,  ", bp.blk_fill);
  printf ("Level = %d,  ", (bp.blk_prop << 1) >> 57);   
  psize = (bp.blk_prop << 32) >> 48;
  printf ("psize %d,  ", psize);
  lsize = (bp.blk_prop << 48) >> 48;
  printf ("lsize %d,  ", lsize);
  pos = 4 * 1024 * 2 + ((bp.blk_dva[which_dva].dva_word[1] << 1ULL) >> 1ULL);
  printf ("pos %d \n", pos);
  
  int comp_data_size = (psize + 1) * 512;
  int data_size = (lsize + 1) * 512;
  *data = (char *) malloc (data_size);
  char *comp_data = (char *) malloc (comp_data_size);
  //fseek (fp, pos * 512, SEEK_SET);
  lseek (fp, pos * 512, SEEK_SET);
  // we just check if lsize and psize are equal -> no compression  
  // we also assume that if compressed, then only lzjb
  if(lsize == psize)
  {
    //fread (*data, sizeof (char), data_size, fp);
    read (fp, *data, sizeof (char)* data_size);
  }
  else
  {
    //fread (comp_data, sizeof (char), comp_data_size, fp);
    read (fp, comp_data, sizeof (char) * comp_data_size);
    lzjb_decompress ((void *) comp_data, (void *) *data, comp_data_size, data_size, 3);
  }
  
  //Store the data size in return value 
  *size = data_size; 

  free(comp_data);
  return pos;
}


int main ()
{
  int i=0;
  fp = open(BLK_LYR_DEVICE, O_RDWR ); //| O_DSYNC );
  
  //directio(fp, DIRECTIO_ON);   
  read (fp, (void *) &vdev_lab, sizeof (vdev_lab[0])* 2);

  lseek (fp, SIZE_RAMDISK - 1024 * 2 * 256, SEEK_SET);
  
  read (fp, (void *) &vdev_lab[2], sizeof (vdev_lab[0])* 2);
  void print_fsinfo ();

  /* Passing the open file pointer to the function. */
  print_fsinfo ();

  //printf ("Char open %d\n",fp2 = open(CHR_LYR_DEVICE, O_RDWR, OTYP_CHR));

  set_corruption("zil_start", 1); 
  traverse_mct();
 
  // we now read the block to be corrupted and corrupt it here
  int offset = 0; 
  int blockno = drv_send(&offset);
  

  //hack: for vdev label corruptions, set block number statically here
  //blockno = 32; offset = 0;
  //blockno = 544; offset = 0;
  //blockno = 130080; offset = 0;
  //blockno = 130592; offset = 0;

  printf("Confirm corruption: block = %d, offset = %d\n", blockno, offset); 
  char buff[512];
  lseek (fp, blockno*512, SEEK_SET);
  read (fp, (void *) buff, 512);
  
  printf("Confirm old buff = \n");
  for(i=0; i<128;i++)
  {
    if(i%4 ==0 )
      printf("%d:  ", i*4);
    printf("%X  ", *(int *)(buff+ 4*i) );
    if((i+1)%4 ==0 )
      printf("\n");
  }

  //change the buffer contents and write back
  int kount=offset;
  //for (kount =0;kount < 64; kount++) 
    *(long long *)&buff[kount] = 0;
  //buff[kount] = 2;
  // For uberblock corruption, we have corrupt the 64 bit txg 
  //*(uint64_t *)&buff[kount] = 0;

  // For zil we corrupt the type to 2 
  //*(uint64_t *)&buff[kount] = 2;


  printf("Confirm buff = \n");
  for(i=0; i<128;i++)
  {
    if(i%4 ==0 )
      printf("%d:  ", i*4); 
    printf("%X  ", *(int *)(buff+ 4*i) ); 
    if((i+1)%4 ==0 )
      printf("\n");
  }

  lseek (fp, blockno*512, SEEK_SET);
 
  int byteswritten = 0; 
  byteswritten+=write(fp, buff, 512); 
  printf("Bytes written = %d\n", byteswritten); 
  
  close (fp);
  // close(fp2);
  return 0;
}

