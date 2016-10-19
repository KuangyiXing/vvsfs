# vvsfs 
 
## remove file function
 
 * Add unlink entry in vvsfs_dir_inode_operations struct  `unlink:     vvsfs_unlink`
 * Construct a vvsfs_unlink function `int vvsfs_unlink(struct inode *dir, struct dentry *dentry)`
    - Method : search in the current directory, find a file the same name as the one going to be deleted. <br>
      Empty its block and remove it from directory entry. <br>
      The files entry behind it in current directory will move up by one position. <br>
      Modify the size of the vvsfs directory and vfs directory<br>
      Decrease the VFS inode count <br>
      Write back to block <br>
 
## make directory function
 
 * Add mkdir entry in vvsfs_dir_inode_operations `mkdir:      vvsfs_mkdir`
 * Construct a vvsfs_mkdir function `static int vvsfs_mkdir(struct inode* dir,struct dentry *dentry,umode_t mode)`
   - Method : construct a new inode. `inode = vvsfs_new_inode(dir,S_IRUGO|S_IWUGO|S_IFDIR);`<br>
              increae the count of inode and directory `inode_inc_link_count(dir);inode_inc_link_count(inode);`<br>
              add the newly created directory's entry into parent directory `strncpy(dent->name, dentry->d_name.name, dentry->d_name.len);dent->inode_number = inode->i_ino;`<br>
              read the newly created inode, initialize it and is_directory is set to 1 `newinodedata.is_directory = 1;`<br>
              modify the size of parent directory's size on both vvsfs and vfs aspect `dir->i_size = inodedata.size;inodedata.size = (num_dirs + 1)*sizeof(struct vvsfs_dir_entry);`<br>
              write back the block <br>
              


## remove directory function 
* Add rmdir entry in vvsfs_dir_inode_operations `rmdir:      vvsfs_rmdir`
* Construct a vvsfs_rmdir function `static int vvsfs_rmdir(struct inode * dir, struct dentry * dentry)`
  -Method : call a function called `vvsfs_empty_dir`to check whether the directory is empty and if it is not emptry, clean the directory <br>
            To check whether the file should be deleted is a directory or a file, if it is a file, empty the content, if it is a directory call vvsfs_empty_dir recursively.<br>
            Write back to block
            
            
## hardlink
* Make a hard link : when creating a hardlink, only create a entry in directory not create a new inode. the inode_num in 
                     vvsfs_dir_entry should be pointing to old entry's inode number.
* Keep hard link in vvsfs : I use a tricky way in this problem. Because hard link only exists on vfs aspect, therefore, eveytime when i umount the file system and mount back again
                            the hard link disappears. A good or easiest way to deal with it is to add attribute in vvsfs_inode struct. However, i always met some problems when adding attributes
                            in struct. Therefore, i chose another way. There is a function called `vvsfs_find_hard_link` to search all files from the root directory to check whether this file has other hard links. 
                            This is a really time consuming algorithm especially when the directory is really big. With regard to check whether the file has hard links, to check whether the inode they point to is the same. If it is the same, then they have hard links. 
                            
            
## proc
* add `proc_create("vvsfsinfo",0,NULL,&vvsfs_proc_fops);` in `__init vvsfs_init`
* To execute `vvsfs_proc_open` function 
` static int vvsfs_proc_open(struct inode *inode, struct file *file)
{       
        return single_open(file, vvsfs_proc_show, NULL);
      
} `

* get a global variable `super_block *sb` then in `vvsfs_fill_super`  `sb = s;`
* in `vvsfs_proc_show` function, read every block to check whether this block is used and its size.
* The method does not support multiple devices very well.
        
    
