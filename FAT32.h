/*
    FAT32.h
    FAT32 filesystem Routines in the PETdisk storage device
    Copyright (C) 2011 Michael Hill

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    Contact the author at bitfixer@bitfixer.com
    http://bitfixer.com
    
    FAT32 code inspired by CC Dharmani's microcontroller blog
    http://www.dharmanitech.com
    
*/

#ifndef _FAT32_H_
#define _FAT32_H_

//Structure to access Master Boot Record for getting info about partitions
struct MBRinfo_Structure{
unsigned char	nothing[446];		//ignore, placed here to fill the gap in the structure
unsigned char	partitionData[64];	//partition records (16x4)
unsigned int	signature;		//0xaa55
};

//Structure to access info of the first partioion of the disk 
struct partitionInfo_Structure{ 				
unsigned char	status;				//0x80 - active partition
unsigned char 	headStart;			//starting head
unsigned int	cylSectStart;		//starting cylinder and sector
unsigned char	type;				//partition type 
unsigned char	headEnd;			//ending head of the partition
unsigned int	cylSectEnd;			//ending cylinder and sector
unsigned long	firstSector;		//total sectors between MBR & the first sector of the partition
unsigned long	sectorsTotal;		//size of this partition in sectors
};

//Structure to access boot sector data
struct BS_Structure{
unsigned char jumpBoot[3]; //default: 0x009000EB
unsigned char OEMName[8];
unsigned int bytesPerSector; //deafault: 512
unsigned char sectorPerCluster;
unsigned int reservedSectorCount;
unsigned char numberofFATs;
unsigned int rootEntryCount;
unsigned int totalSectors_F16; //must be 0 for FAT32
unsigned char mediaType;
unsigned int FATsize_F16; //must be 0 for FAT32
unsigned int sectorsPerTrack;
unsigned int numberofHeads;
unsigned long hiddenSectors;
unsigned long totalSectors_F32;
unsigned long FATsize_F32; //count of sectors occupied by one FAT
unsigned int extFlags;
unsigned int FSversion; //0x0000 (defines version 0.0)
unsigned long rootCluster; //first cluster of root directory (=2)
unsigned int FSinfo; //sector number of FSinfo structure (=1)
unsigned int BackupBootSector;
unsigned char reserved[12];
unsigned char driveNumber;
unsigned char reserved1;
unsigned char bootSignature;
unsigned long volumeID;
unsigned char volumeLabel[11]; //"NO NAME "
unsigned char fileSystemType[8]; //"FAT32"
unsigned char bootData[420];
unsigned int bootEndSignature; //0xaa55
};


//Structure to access FSinfo sector data
struct FSInfo_Structure
{
unsigned long leadSignature; //0x41615252
unsigned char reserved1[480];
unsigned long structureSignature; //0x61417272
unsigned long freeClusterCount; //initial: 0xffffffff
unsigned long nextFreeCluster; //initial: 0xffffffff
unsigned char reserved2[12];
unsigned long trailSignature; //0xaa550000
};

//Structure to access Directory Entry in the FAT
struct dir_Structure{
unsigned char name[11];
unsigned char attrib; //file attributes
unsigned char NTreserved; //always 0
unsigned char timeTenth; //tenths of seconds, set to 0 here
unsigned int createTime; //time file was created
unsigned int createDate; //date file was created
unsigned int lastAccessDate;
unsigned int firstClusterHI; //higher word of the first cluster number
unsigned int writeTime; //time of last write
unsigned int writeDate; //date of last write
unsigned int firstClusterLO; //lower word of the first cluster number
unsigned long fileSize; //size of file in bytes
};

struct dir_Longentry_Structure{
    unsigned char LDIR_Ord;
    unsigned int LDIR_Name1[5];
    unsigned char LDIR_Attr;
    unsigned char LDIR_Type;
    unsigned char LDIR_Chksum;
    unsigned int LDIR_Name2[6];
    unsigned int LDIR_FstClusLO;
    unsigned int LDIR_Name3[2];
};

// structure for file read information
typedef struct _file_stat{
    unsigned long currentCluster;
    unsigned long fileSize;
    unsigned long currentSector;
    unsigned long byteCounter;
    int sectorIndex;
} file_stat;

typedef struct _dir_position {
    unsigned long cluster;
    unsigned long sector;
};

//Attribute definitions for file/directory
#define ATTR_READ_ONLY     0x01
#define ATTR_HIDDEN        0x02
#define ATTR_SYSTEM        0x04
#define ATTR_VOLUME_ID     0x08
#define ATTR_DIRECTORY     0x10
#define ATTR_ARCHIVE       0x20
#define ATTR_LONG_NAME     0x0f


#define DIR_ENTRY_SIZE     0x32
#define EMPTY              0x00
#define DELETED            0xe5
#define GET     0
#define SET     1
#define READ	0
#define VERIFY  1
#define ADD		0
#define REMOVE	1
#define LOW		0
#define HIGH	1	
#define TOTAL_FREE   1
#define NEXT_FREE    2
#define GET_LIST     0
#define GET_FILE     1
#define DELETE		 2
#define EOF		0x0fffffff


//************* external variables *************
volatile unsigned long _firstDataSector,     _rootCluster,        _totalClusters;
volatile unsigned int  _bytesPerSector,      _sectorPerCluster,   _reservedSectorCount;
volatile unsigned long _unusedSectors, _appendFileSector, _appendFileLocation, _fileSize, _appendStartCluster;

//global flag to keep track of free cluster count updating in FSinfo sector
unsigned char _freeClusterCountUpdated;
volatile unsigned long _fileStartCluster;


//************* functions *************
unsigned char getBootSectorData (void);
unsigned long getFirstSector(unsigned long clusterNumber);
unsigned long getSetFreeCluster(unsigned char totOrNext, unsigned char get_set, unsigned long FSEntry);
struct dir_Structure* findFiles (unsigned char flag, unsigned char *fileName);
struct dir_Structure* findFiles2 (unsigned char flag, unsigned char *fileName, unsigned char cmp_long_fname, unsigned long firstCluster);
unsigned long getSetNextCluster (unsigned long clusterNumber,unsigned char get_set,unsigned long clusterEntry);
unsigned char readFile (unsigned char flag, unsigned char *fileName);
unsigned char convertFileName (unsigned char *fileName);
void writeFile (unsigned char *fileName);
void appendFile (void);
unsigned long searchNextFreeCluster (unsigned long startCluster);
void memoryStatistics (void);
void displayMemory (unsigned char flag, unsigned long memory);
void deleteFile (unsigned char *fileName);
void freeMemoryUpdate (unsigned char flag, unsigned long size);
unsigned char ChkSum (unsigned char *pFcbName);

void startFileRead(struct dir_Structure *dirEntry, file_stat *thisFileStat);
void getCurrentFileBlock(file_stat *thisFileStat);
unsigned long getNextBlockAddress(file_stat *thisFileStat);

#endif
