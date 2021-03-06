/*
 * A Very Very Simple Filesystem
   Eric McCreath 2006, 2008, 2010 - GPL

   (based on the simplistic RAM filesystem McCreath 2001)   */

/* to make use:
      make -C /usr/src/linux-headers-2.6.32-23-generic/  SUBDIRS=$PWD modules
	  (or just make, with the accompanying Makefile)

   to load use:
      sudo insmod vvsfs.ko
	  (may need to copy vvsfs.ko to a local filesystem first)

   to make a suitable filesystem:
      dd of=myvvsfs.raw if=/dev/zero bs=512 count=100
      ./mkfs.vvsfs myvvsfs.raw
	  (could also use a USB device etc.)

   to mount use:
      mkdir testdir
      sudo mount -o loop -t vvsfs myvvsfs.raw testdir

   to use a USB device:
      create a suitable partition on USB device (exercise for reader)
	  ./mkfs.vvsfs /dev/sdXn
      where sdXn is the device name of the usb drive
      mkdir testdir
      sudo mount -t vvsfs /dev/sdXn testdir

   use the file system:
      cd testdir
      echo hello > file1
      cat file1
      cd ..

   unmount the filesystem:
      sudo umount testdir

   remove the module:
      sudo rmmod vvsfs 
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/statfs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>

#include "vvsfs.h"

#define DEBUG 1

static struct inode_operations vvsfs_file_inode_operations;
static struct file_operations vvsfs_file_operations;
static struct super_operations vvsfs_ops;
static struct file_operations vvsfs_dir_operations;
static struct inode_operations vvsfs_dir_inode_operations;
struct inode * vvsfs_new_inode(const struct inode *, umode_t);
static int vvsfs_unlink(struct inode *, struct dentry *);
static struct super_block * sb;

int vvsfs_find_hard_link(struct inode *, struct dentry *);

static int vvsfs_fill_super(struct super_block *, void *, int);

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);
static void
vvsfs_put_super(struct super_block *sb) {
  if (DEBUG) printk("vvsfs - put_super\n");
  return;
}

static int 
vvsfs_statfs(struct dentry *dentry, struct kstatfs *buf) {
  if (DEBUG) printk("vvsfs - statfs\n");

  buf->f_namelen = MAXNAME;
  return 0;
}

// vvsfs_readblock - reads a block from the block device (this will copy over
//                      the top of inode)
static int
vvsfs_readblock(struct super_block *sb, int inum, struct vvsfs_inode *inode) {  // reference to a super block sitting in the VFS;inode number ;the block of inode you are reading
  struct buffer_head *bh;

  if (DEBUG) printk("vvsfs - readblock : %d\n", inum);
  
  bh = sb_bread(sb,inum);//initiate the block read of super block, bh is buffer head, stores the information about the buffer

  // bh->b_data is part of information of that buffer
  memcpy((void *) inode, (void *) bh->b_data, BLOCKSIZE); //copy the b_data to the inode struct

  brelse(bh);//release the buffer head. if not, will cause memory leak.
  if (DEBUG) printk("vvsfs - readblock done : %d\n", inum);
  return BLOCKSIZE;
}

// vvsfs_writeblock - write a block from the block device(this will just mark the block
//                      as dirtycopy)
static int
vvsfs_writeblock(struct super_block *sb, int inum, struct vvsfs_inode *inode) {
  struct buffer_head *bh;

  if (DEBUG) printk("vvsfs - writeblock : %d\n", inum);

  bh = sb_bread(sb,inum); //get hold of that buffer

  memcpy(bh->b_data, inode, BLOCKSIZE);//copy the inode data to the buffer head

  mark_buffer_dirty(bh); // mark that buffer dirty, changed
  sync_dirty_buffer(bh);  //force to write back to the actual hard disk
  brelse(bh);
  if (DEBUG) printk("vvsfs - writeblock done: %d\n", inum);
  return BLOCKSIZE;
}


//vvsfs_mkdir - make a directory - similar to create a file in directory
static int vvsfs_mkdir(struct inode* dir,struct dentry *dentry,umode_t mode){
      
   struct vvsfs_inode inodedata;
   struct vvsfs_dir_entry *dent;
   struct vvsfs_inode newinodedata;

   struct inode * inode = NULL;

   int num_dirs;
   
   if (DEBUG) printk("vvsfs - make dir : %s\n",dentry->d_name.name);
 
   inode_inc_link_count(dir);
  
   inode = vvsfs_new_inode(dir,S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR);

   

   if(!inode) return -ENOSPC;
   inode->i_op = &vvsfs_dir_inode_operations;
   inode->i_fop = &vvsfs_dir_operations;


   inode_inc_link_count(inode);

   /*get a vfs inode */

   if (!dir) return -1;
   

   vvsfs_readblock(dir->i_sb,dir->i_ino, &inodedata);
   
   num_dirs = inodedata.size/sizeof(struct vvsfs_dir_entry);

   dent = (struct vvsfs_dir_entry *) ((inodedata.data) + num_dirs*sizeof(struct vvsfs_dir_entry));


   strncpy(dent->name, dentry->d_name.name, dentry->d_name.len);
   dent->name[dentry->d_name.len] = '\0';

   inodedata.size = (num_dirs + 1)*sizeof(struct vvsfs_dir_entry);
   
   dent->inode_number = inode->i_ino;

   vvsfs_readblock(inode->i_sb,inode->i_ino,&newinodedata);
   newinodedata.is_directory = 1;
   vvsfs_writeblock(inode->i_sb, inode->i_ino,&newinodedata);
   
   dir->i_size = inodedata.size;
   mark_inode_dirty(dir);

   vvsfs_writeblock(dir->i_sb,dir->i_ino,&inodedata);
   d_instantiate(dentry,inode);

   printk("Directory created %ld\n",inode->i_ino);
   return 0;
}



//vvsfs_empty_dir -to check whether the directory is empty and if it is not emptry, clean the directory
static int vvsfs_empty_dir(struct inode *dir){
     
      struct vvsfs_inode inodedata;   //this is directory data
      struct vvsfs_dir_entry * dent;
      struct vvsfs_inode newinodedata;  //this is the each file data in the directory
      struct inode * inode;
      
      int k,num_dirs;
      vvsfs_readblock(dir->i_sb, dir->i_ino,&inodedata);//get the directory data
      num_dirs = inodedata.size/sizeof(struct vvsfs_dir_entry);

       for (k=0;k < num_dirs;k++) {

             dent = (struct vvsfs_dir_entry *) ((inodedata.data) + k*sizeof(struct vvsfs_dir_entry));   // get each file directory entry in the directory
             
             inode = vvsfs_iget(dir->i_sb, dent->inode_number); // get each file's inode 
            
              vvsfs_readblock(inode->i_sb,inode->i_ino,&newinodedata); //get each file data in the directory


              if(newinodedata.is_directory == 1) vvsfs_empty_dir(inode);//check whether it is directory
              
              
              memset(newinodedata.data,0,sizeof(newinodedata.data));
              newinodedata.size = 0;
              newinodedata.is_empty = 1;
              newinodedata.is_directory = 0;

              vvsfs_writeblock(inode->i_sb,inode->i_ino,&newinodedata);
}
 
      inodedata.is_directory = 0;
      vvsfs_writeblock(dir->i_sb,dir->i_ino,&inodedata);
      return 0;
}



//vvsfs_rmdir  - remove directory from the directory
static int vvsfs_rmdir(struct inode * dir, struct dentry * dentry){

   struct inode * inode = dentry->d_inode;
   
   int err = -ENOTEMPTY;
   if( vvsfs_empty_dir(inode) == 0){
   err = vvsfs_unlink(dir,dentry);
     if(!err){
          inode->i_size = 0;
          inode_dec_link_count(inode);
          inode_dec_link_count(dir);
          return 0;}
    }
    return err;

}




static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
// vvsfs_readdir - reads a directory and places the result using filldir
vvsfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
#else
vvsfs_readdir(struct file *filp, struct dir_context *ctx)
#endif
{
	struct inode *i;
	struct vvsfs_inode dirdata;
	int num_dirs;
	struct vvsfs_dir_entry *dent;
	int error, k;

	if (DEBUG) printk("vvsfs - readdir\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	i = filp->f_dentry->d_inode;
#else
	i = file_inode(filp);
#endif
	vvsfs_readblock(i->i_sb, i->i_ino, &dirdata);
	num_dirs = dirdata.size / sizeof(struct vvsfs_dir_entry);

	if (DEBUG) printk("Number of entries %d fpos %Ld\n", num_dirs, filp->f_pos);

	error = 0;
	k=0;
	dent = (struct vvsfs_dir_entry *) &dirdata.data;
	while (!error && filp->f_pos < dirdata.size && k < num_dirs) {
		printk("adding name : %s ino : %d\n",dent->name, dent->inode_number);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
		error = filldir(dirent, 
		    dent->name, strlen(dent->name), filp->f_pos, dent->inode_number,DT_REG);
		if (error)
			break;
		filp->f_pos += sizeof(struct vvsfs_dir_entry);
#else
		if (dent->inode_number) {
			if (!dir_emit (ctx, dent->name, strnlen (dent->name, MAXNAME),
				dent->inode_number, DT_UNKNOWN)) return 0;
		}
		ctx->pos += sizeof(struct vvsfs_dir_entry);
#endif
		k++;
		dent++;
	}
        
        i->i_size = dirdata.size;
        mark_inode_dirty(i);
	// update_atime(i);
	printk("done readdir\n");

	return 0;
}

// vvsfs_lookup - A directory name in a directory. It basically attaches the inode 
//                of the file to the directory entry.
static struct dentry *
vvsfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{

  int num_dirs;
  int k;
  struct vvsfs_inode dirdata;
  struct inode *inode = NULL;
  struct vvsfs_dir_entry *dent;

  if (DEBUG) printk("vvsfs - lookup\n");

  vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
  num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);

  for (k=0;k < num_dirs;k++) {
    dent = (struct vvsfs_dir_entry *) ((dirdata.data) + k*sizeof(struct vvsfs_dir_entry));
    
    if ((strlen(dent->name) == dentry->d_name.len) && 
	strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) == 0) {
      inode = vvsfs_iget(dir->i_sb, dent->inode_number);
 
      if (!inode)
	return ERR_PTR(-EACCES);
    
      d_add(dentry, inode);
      return NULL;

    }
  }
  d_add(dentry, inode);
  return NULL;
}


 int vvsfs_add_link (struct dentry *dentry, struct inode *inode){
 
    struct inode *dir = dentry->d_parent->d_inode;
    struct vvsfs_dir_entry *dent;
    int num_dirs;
    
    struct vvsfs_inode inodedata;
    struct vvsfs_inode newinodedata;
    vvsfs_readblock(dir->i_sb,dir->i_ino,&inodedata);
      
    num_dirs = inodedata.size/sizeof(struct vvsfs_dir_entry);
 
 
    dent = (struct vvsfs_dir_entry *) ((inodedata.data) + num_dirs*sizeof(struct vvsfs_dir_entry));
 
 
    strncpy(dent->name, dentry->d_name.name, dentry->d_name.len);
 
    dent->name[dentry->d_name.len] = '\0';
 
    dent->inode_number = inode->i_ino;
 
 
    inodedata.size = (num_dirs + 1)*sizeof(struct vvsfs_dir_entry);
    
    
    dir->i_size = inodedata.size;
    mark_inode_dirty(dir);
 
    vvsfs_writeblock(dir->i_sb,dir->i_ino,&inodedata);
 
    vvsfs_readblock(inode->i_sb,inode->i_ino,&newinodedata);
    

    vvsfs_writeblock(inode->i_sb,inode->i_ino,&newinodedata);
 
    return 0;
 }
 
 
 
 
 static int vvsfs_link(struct dentry * old_dentry, struct inode *dir, struct dentry *dentry){
 
     struct inode *inode = old_dentry->d_inode;
     int err;
 
     inode->i_ctime = CURRENT_TIME_SEC;
 
     inode_inc_link_count(inode);
    
     mark_inode_dirty(inode);
     ihold(inode);
    
     
     err = vvsfs_add_link(dentry, inode);

   if (!err) {
 		d_instantiate(dentry, inode);
 		return 0;
  	}
 
    inode_dec_link_count(inode);
    iput(inode); 
    return err;
 }







static int vvsfs_unlink(struct inode *dir, struct dentry *dentry){
        
   int num_dirs;
   int num_hardlinks = 0;
   int k, delindex, del_ino;
 
   struct vvsfs_inode inodedata;
   struct inode *inode = NULL;
   struct vvsfs_dir_entry *dent;


   struct inode *root;


 if(DEBUG) printk("delete file\n");

 vvsfs_readblock(dir->i_sb, dir->i_ino, &inodedata);
 num_dirs = inodedata.size/sizeof(struct vvsfs_dir_entry);
 delindex = -1;
 

 for (k=0;k < num_dirs;k++) {

    dent = (struct vvsfs_dir_entry *) ((inodedata.data) + k*sizeof(struct vvsfs_dir_entry));
    
         if (delindex == -1 && (strlen(dent->name) == dentry->d_name.len) &&  strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) == 0) {
               inode = vvsfs_iget(dir->i_sb, dent->inode_number);
     
              if (!inode)  return -ENOENT;
              del_ino = dent->inode_number;
              delindex = k;
                   }
        if (delindex != -1)
             memcpy(dent,(struct vvsfs_dir_entry *)((inodedata.data)+ (k+1)*sizeof(struct vvsfs_dir_entry)),sizeof(struct vvsfs_dir_entry));

}


    if (delindex != -1){
      inodedata.size = inodedata.size - sizeof(struct vvsfs_dir_entry);
      dir->i_size = inodedata.size;
      vvsfs_writeblock(dir->i_sb,dir->i_ino,&inodedata);

      vvsfs_readblock(inode->i_sb,del_ino,&inodedata);
    

      root = vvsfs_iget(dir->i_sb,0);//get the root directory
      num_hardlinks = vvsfs_find_hard_link(root,dentry) + 1;
    
  printk("hard link is %i",num_hardlinks);

      
     if(num_hardlinks == 1)
      {memset(inodedata.data,0,sizeof(inodedata.data));
      inodedata.size = 0;
      inodedata.is_empty = 1;}
   


      vvsfs_writeblock(dir->i_sb,del_ino, &inodedata);

      inode_dec_link_count(inode);
      mark_inode_dirty(inode);
      
      
      return 0;

    }
else {
    return -ENOENT;
}

}
// vvsfs_empty_inode - finds the first free inode (returns -1 is unable to find one)
static int vvsfs_empty_inode(struct super_block *sb) {
  struct vvsfs_inode block;
  int k;
  for (k =0;k<NUMBLOCKS;k++) {
    vvsfs_readblock(sb,k,&block);
    if (block.is_empty) return k;   
  }
  return -1;
}

// vvsfs_new_inode - find and construct a new inode.
struct inode * vvsfs_new_inode(const struct inode * dir, umode_t mode)
{
  struct vvsfs_inode block;
  struct super_block * sb;
  struct inode * inode;
  int newinodenumber;

  if (DEBUG) printk("vvsfs - new inode\n");
  
  if (!dir) return NULL;
  sb = dir->i_sb;

  /* get an vfs inode */
  inode = new_inode(sb);
  if (!inode) return NULL;
 
  /* find a spare inode in the vvsfs */
  newinodenumber = vvsfs_empty_inode(sb);
  if (newinodenumber == -1) {
    printk("vvsfs - inode table is full.\n");
    return NULL;
  }
  
  block.is_empty = false;
  block.size = 0;
  block.is_directory = false;
  
  vvsfs_writeblock(sb,newinodenumber,&block);
  
  inode_init_owner(inode, dir, mode);
  inode->i_ino = newinodenumber;
  inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;
   
  inode->i_op = NULL;
  
  insert_inode_hash(inode);
  
  return inode;
}

//vvsfs_truncate  - truncate the file
void vvsfs_truncate(struct inode * inode, loff_t size)
{

        struct vvsfs_inode inodedata;
      

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)))
		return;


        vvsfs_readblock(inode->i_sb,inode->i_ino,&inodedata);

        //inodedata.data[size] = '\0';
       
      
        memset(&inodedata.data[size],0,inodedata.size - size);
  
        inodedata.size = (int )size;

      vvsfs_writeblock(inode->i_sb,inode->i_ino,&inodedata);
       
         
} 


//vvsfs_find_hard_link - because everytime the filesystem is umounted, the cache version about hard link of the file inode will be lost, so this function can promise that the number of hard link of file inode is consistent between each mount.

int vvsfs_find_hard_link(struct inode *dir, struct dentry *dentry)
{   
   struct inode *inode;

   struct vvsfs_inode dirdata;
   struct vvsfs_inode inodedata;

   struct vvsfs_dir_entry *dent;

   int nlink = 0;
   int k,num_dirs;


      vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
      num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
      
       for (k=0;k < num_dirs;k++) {

          dent = (struct vvsfs_dir_entry *) ((dirdata.data) + k*sizeof(struct vvsfs_dir_entry));
          inode = vvsfs_iget(dir->i_sb, dent->inode_number);

          vvsfs_readblock(inode->i_sb,inode->i_ino,&inodedata); 
           
          if(inodedata.is_directory == 1){ nlink += vvsfs_find_hard_link(inode,dentry);}
     
          else{
                if(dent->inode_number == dentry->d_inode->i_ino && ( strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) != 0 || dir->i_ino != dentry->d_parent->d_inode->i_ino) ) nlink ++;}
//this "if" is checking whether exists any other file has the same inode number, if there exists, add the number of hard link; 

          }

   
       
      return nlink;


}



//vvsfs_getattr -when the file inode information is updated, this function will be executed everytime

int vvsfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct super_block *sb = dentry->d_sb;
      //  struct inode *dir = dentry->d_parent->d_inode;
        struct inode *root = vvsfs_iget(sb,0);//get the root directory
	generic_fillattr(dentry->d_inode, stat);
	//stat->blocks = (BLOCK_SIZE / 512) * V1_minix_blocks(stat->size, sb);


        stat->nlink = vvsfs_find_hard_link(root,dentry) + 1;

        
	stat->blksize = sb->s_blocksize;
	return 0;
}




//vvsfs_setattr  - set attributes to the file
static int vvsfs_setattr(struct dentry *dentry, struct iattr *attr){
   
   struct inode *inode = dentry->d_inode;
   int error;

   error = inode_change_ok(inode, attr);

   if(error) return error;
   
   if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(inode, attr->ia_size);
		vvsfs_truncate(inode,attr->ia_size);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;

}

// vvsfs_create - create a new file in a directory 
static int
vvsfs_create(struct inode *dir, struct dentry* dentry, umode_t mode, bool excl)
{
  struct vvsfs_inode dirdata;
  int num_dirs;
  struct vvsfs_dir_entry *dent;

  struct inode * inode;

  if (DEBUG) printk("vvsfs - create : %s\n",dentry->d_name.name);

  inode = vvsfs_new_inode(dir, S_IRUGO|S_IWUGO|S_IFREG);

  if (!inode)
    return -ENOSPC;
  inode->i_op = &vvsfs_file_inode_operations;
  inode->i_fop = &vvsfs_file_operations;
  inode->i_mode = mode;

  /* get an vfs inode */
  if (!dir) return -1;

  vvsfs_readblock(dir->i_sb,dir->i_ino,&dirdata);
  num_dirs = dirdata.size/sizeof(struct vvsfs_dir_entry);
  dent = (struct vvsfs_dir_entry *) ((dirdata.data) + num_dirs*sizeof(struct vvsfs_dir_entry));

  strncpy(dent->name, dentry->d_name.name,dentry->d_name.len);
  dent->name[dentry->d_name.len] = '\0';
  

  dirdata.size = (num_dirs + 1) * sizeof(struct vvsfs_dir_entry);
 

  dent->inode_number = inode->i_ino;
  
  
  dir->i_size = dirdata.size;
  mark_inode_dirty(dir);
  
  vvsfs_writeblock(dir->i_sb,dir->i_ino,&dirdata);

  d_instantiate(dentry, inode);

  
  printk("File created %ld\n",inode->i_ino);
  return 0;
}

// vvsfs_file_write - write to a file
static ssize_t
vvsfs_file_write(struct file *filp, const char *buf, size_t count, loff_t *ppos) // a cache version of metadata of the file; user space the data was written; how much data there is; offset about where in the file begin to write
{
  struct vvsfs_inode filedata; 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
  struct inode *inode = filp->f_dentry->d_inode;
#else
  struct inode *inode = filp->f_path.dentry->d_inode; // the cache version of inode for that file, it changes depending on the kernel version
#endif
  ssize_t pos;
  struct super_block * sb;
  char * p;

  if (DEBUG) printk("vvsfs - file write - count : %zu ppos %Ld\n",count,*ppos);

  if (!inode) {
    printk("vvsfs - Problem with file inode\n");
    return -EINVAL;
  }
  
  if (!(S_ISREG(inode->i_mode))) {
    printk("vvsfs - not regular file\n");
    return -EINVAL;
  }
  if (*ppos > inode->i_size || count <= 0) {
    printk("vvsfs - attempting to write over the end of a file.\n");
    return 0;
  }  
  sb = inode->i_sb;

  vvsfs_readblock(sb,inode->i_ino,&filedata);//copy the block from the hard disk into a cache version. Writing means you have to read the data in first

  if (filp->f_flags & O_APPEND)
    pos = inode->i_size; //start at the end of our file
  else
    pos = *ppos;

  if (pos + count > MAXFILESIZE) return -ENOSPC; //return an error

  filedata.size = pos+count;// modify the filesize in cache version
  p = filedata.data + pos; 
  if (copy_from_user(p,buf,count))//copy the data from buffer to the position
    return -ENOSPC;
  *ppos = pos; // move the file index to the right spot.
  buf += count;

  inode->i_size = filedata.size;  //reset the size in underline version in hard disk

  vvsfs_writeblock(sb,inode->i_ino,&filedata); //write the block 
  
  if (DEBUG) printk("vvsfs - file write done : %zu ppos %Ld\n",count,*ppos);
  
  return count;
}

// vvsfs_file_read - read data from a file
static ssize_t
vvsfs_file_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
  struct vvsfs_inode filedata; 
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
  struct inode *inode = filp->f_dentry->d_inode;
#else
  struct inode *inode = filp->f_path.dentry->d_inode;
#endif
  char                    *start;
  ssize_t                  offset, size;

  struct super_block * sb;
  
  if (DEBUG) printk("vvsfs - file read - count : %zu ppos %Ld\n",count,*ppos);

  if (!inode) {
    printk("vvsfs - Problem with file inode\n");
    return -EINVAL;
  }
  
  if (!(S_ISREG(inode->i_mode))) {
    printk("vvsfs - not regular file\n");
    return -EINVAL;
  }
  if (*ppos > inode->i_size || count <= 0) {
    printk("vvsfs - attempting to write over the end of a file.\n");
    return 0;
  }  
  sb = inode->i_sb;

  printk("r : readblock\n");
  vvsfs_readblock(sb,inode->i_ino,&filedata);

  start = buf;
   printk("rr\n");
  size = MIN (inode->i_size - *ppos,count);

  printk("readblock : %zu\n", size);
  offset = *ppos;            
  *ppos += size;

  printk("r copy_to_user\n");

  if (copy_to_user(buf,filedata.data + offset,size)) 
    return -EIO;
  buf += size;
  
  printk("r return\n");
  return size;
}

//vvsfs_proc_show - cat /proc/vvsfsinfo can see how many inode has been used
static int vvsfs_proc_show(struct seq_file *m, void *v )
{
	int num_inodes = 0;// the number of inodes not empty
        int size = 0;
        int i;
        struct inode *inode;
        struct vvsfs_inode inodedata;


       for(i = 0;i < 100;i++){ //to check our 100 blocks
          inode = vvsfs_iget(sb,i);
          vvsfs_readblock(inode->i_sb, inode->i_ino,&inodedata);
          if(inodedata.is_empty == 0) 
          {
           num_inodes ++;  
           size += inodedata.size;  }

         }      
        
        
     
   
          
          seq_printf(m,"Used Inodes:%i \nUsed memory: %i \n",num_inodes,size);
       
	return 0;
        
}


//vvsfs_proc_open  - to execute vvsfs_proc_show function
static int vvsfs_proc_open(struct inode *inode, struct file *file)
{       
        return single_open(file, vvsfs_proc_show, NULL);
     
}

static struct file_operations vvsfs_file_operations = {
        read: vvsfs_file_read,        /* read */
        write: vvsfs_file_write,       /* write */
       
};

static struct inode_operations vvsfs_file_inode_operations = {

        setattr :   vvsfs_setattr,   /*  truncate */
        getattr :   vvsfs_getattr,
};                                                                                                                                                            

static struct file_operations vvsfs_dir_operations = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	.readdir =	vvsfs_readdir,          /* readdir */
#else
	.llseek =	generic_file_llseek,
	.read	=	generic_read_dir,
	.iterate =	vvsfs_readdir,
	.fsync	=	generic_file_fsync,
#endif
};

static struct inode_operations vvsfs_dir_inode_operations = {
   create:     vvsfs_create,                   /* create */
   lookup:     vvsfs_lookup,           /* lookup */
   link  :     vvsfs_link,             /* link   */
   unlink:     vvsfs_unlink,           /* unlink */
   mkdir:      vvsfs_mkdir,            /* make directory */
   rmdir:      vvsfs_rmdir,            /* remove directory */
};

static const struct file_operations vvsfs_proc_fops = {
	.open		= vvsfs_proc_open,
        .read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

// vvsfs_iget - get the inode from the super block
struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct vvsfs_inode filedata; 

    if (DEBUG) {
        printk("vvsfs - iget - ino : %d", (unsigned int) ino);
        printk(" super %p\n", sb);  
    }

    inode = iget_locked(sb, ino);
    if(!inode)
        return ERR_PTR(-ENOMEM);
    if(!(inode->i_state & I_NEW))
        return inode;

    vvsfs_readblock(inode->i_sb,inode->i_ino,&filedata);

	inode->i_size = filedata.size;
 
//	inode->i_uid = (kuid_t) 0;
//	inode->i_gid = (kgid_t) 0;

	inode->i_ctime = inode->i_mtime = inode->i_atime = CURRENT_TIME;

    if (filedata.is_directory) {
        inode->i_mode = S_IRUGO|S_IWUGO|S_IFDIR;
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    } else {
        inode->i_mode = S_IRUGO|S_IWUGO|S_IFREG;
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
    }

    unlock_new_inode(inode);
    return inode;
}

// vvsfs_fill_super - read the super block (this is simple as we do not
//                    have one in this file system)
static int vvsfs_fill_super(struct super_block *s, void *data, int silent)
{
  struct inode *i;
  int hblock;

  if (DEBUG) printk("vvsfs - fill super\n");

  s->s_flags = MS_NOSUID | MS_NOEXEC;
  s->s_op = &vvsfs_ops;

  i = new_inode(s);

  i->i_sb = s;
  i->i_ino = 0;
  i->i_flags = 0;
  i->i_mode = S_IRUGO|S_IWUGO|S_IXUGO|S_IFDIR;
  i->i_op = &vvsfs_dir_inode_operations;
  i->i_fop = &vvsfs_dir_operations; 
  printk("inode %p\n", i);

  hblock = bdev_logical_block_size(s->s_bdev);
  if (hblock > BLOCKSIZE) {
     printk("device blocks are too small!!");
     return -1;
  }

  set_blocksize(s->s_bdev, BLOCKSIZE);
  s->s_blocksize = BLOCKSIZE;
  s->s_blocksize_bits = BLOCKSIZE_BITS;
  s->s_root = d_make_root(i);
  
  sb = s;

  return 0;
}

static struct super_operations vvsfs_ops = {
  statfs: vvsfs_statfs,
  put_super: vvsfs_put_super,
};

static struct dentry *vvsfs_mount(struct file_system_type *fs_type,
				    int flags, const char *dev_name, void *data)
{
  return mount_bdev(fs_type, flags, dev_name, data, vvsfs_fill_super);
} // it will execute vvsfs_fill_super function

static struct file_system_type vvsfs_type = {
  .owner	= THIS_MODULE,
  .name		= "vvsfs",   // the name when i do cat /proc/filesystsms
  .mount	= vvsfs_mount,  //when we mount the filesystem, it will execute the above vvsfs_mount function
  .kill_sb	= kill_block_super,
  .fs_flags	= FS_REQUIRES_DEV,
};

static int __init vvsfs_init(void)
{
  printk("Registering vvsfs\n");
  proc_create("vvsfsinfo",0,NULL,&vvsfs_proc_fops);
  return register_filesystem(&vvsfs_type);/* this point to the vvsfs_type, which is above */ 
}

static void __exit vvsfs_exit(void)
{
  printk("Unregistering the vvsfs.\n");
  unregister_filesystem(&vvsfs_type);
}

module_init(vvsfs_init);
module_exit(vvsfs_exit);
MODULE_LICENSE("GPL");
