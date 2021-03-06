#define BLOCKSIZE 512
#define BLOCKSIZE_BITS 8
#define NUMBLOCKS 100
#define MAXNAME 15

#define MAXFILESIZE (BLOCKSIZE - 3*sizeof(int))

#define MIN(a,b) (((a)<(b))?(a):(b))


#define true 1
#define false 0


struct vvsfs_inode {
  int is_empty;
  int is_directory; // 1 means it is a directory, 0 means it is a normal file
  int size;  // how big the file is
  char data[MAXFILESIZE];
};  //this inode has the metadata of the file and also the content of the file 

struct vvsfs_dir_entry {
  char name[MAXNAME+1];
  int inode_number;
};
