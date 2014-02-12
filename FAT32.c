/*
    FAT32.c
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

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "FAT32.h"
#include "UART_routines.h"
#include "SD_routines.h"

/*
void mymemset(unsigned char *ptr, unsigned char val, int num)
{
    int i;
    for (i = 0; i < num; i++)
    {
        ptr[i] = val;
    }
}
*/

/*
int mystrncmp(unsigned char *a, unsigned char *b, int count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
    }
    return 1;
}
*/

//***************************************************************************
//Function: to read data from boot sector of SD card, to determine important
//parameters like _bytesPerSector, sectorsPerCluster etc.
//Arguments: none
//return: none
//***************************************************************************
unsigned char getBootSectorData (void)
{
    struct BS_Structure *bpb; //mapping the buffer onto the structure
    struct MBRinfo_Structure *mbr;
    struct partitionInfo_Structure *partition;
    unsigned long dataSectors;

    _unusedSectors = 0;

    SD_readSingleBlock(0);
    bpb = (struct BS_Structure *)_buffer;

    if(bpb->jumpBoot[0]!=0xE9 && bpb->jumpBoot[0]!=0xEB)   //check if it is boot sector
    {
      mbr = (struct MBRinfo_Structure *) _buffer;       //if it is not boot sector, it must be MBR
      
      if(mbr->signature != 0xaa55) return 1;       //if it is not even MBR then it's not FAT32
      	
      partition = (struct partitionInfo_Structure *)(mbr->partitionData);//first partition
      _unusedSectors = partition->firstSector; //the unused sectors, hidden to the FAT
      
      SD_readSingleBlock(partition->firstSector);//read the bpb sector
      bpb = (struct BS_Structure *)_buffer;
      if(bpb->jumpBoot[0]!=0xE9 && bpb->jumpBoot[0]!=0xEB) return 1; 
    }

    _bytesPerSector = bpb->bytesPerSector;
    _sectorPerCluster = bpb->sectorPerCluster;
    _reservedSectorCount = bpb->reservedSectorCount;
    _rootCluster = bpb->rootCluster;// + (sector / _sectorPerCluster) +1;
    _firstDataSector = bpb->hiddenSectors + _reservedSectorCount + (bpb->numberofFATs * bpb->FATsize_F32);

    dataSectors = bpb->totalSectors_F32
                  - bpb->reservedSectorCount
                  - ( bpb->numberofFATs * bpb->FATsize_F32);
    _totalClusters = dataSectors / _sectorPerCluster;

    if((getSetFreeCluster (TOTAL_FREE, GET, 0)) > _totalClusters)  //check if FSinfo free clusters count is valid
    {
         _freeClusterCountUpdated = 0;
    }
    else
    {
    	 _freeClusterCountUpdated = 1;
    }
    return 0;
}

//***************************************************************************
//Function: to calculate first sector address of any given cluster
//Arguments: cluster number for which first sector is to be found
//return: first sector address
//***************************************************************************

unsigned long getFirstSector(unsigned long clusterNumber)
{
  return (((clusterNumber - 2) * _sectorPerCluster) + _firstDataSector);
}

//***************************************************************************
//Function: get cluster entry value from FAT to find out the next cluster in the chain
//or set new cluster entry in FAT
//Arguments: 1. current cluster number, 2. get_set (=GET, if next cluster is to be found or = SET,
//if next cluster is to be set 3. next cluster number, if argument#2 = SET, else 0
//return: next cluster number, if if argument#2 = GET, else 0
//****************************************************************************
unsigned long getSetNextCluster (unsigned long clusterNumber,
                                 unsigned char get_set,
                                 unsigned long clusterEntry)
{
    unsigned int FATEntryOffset;
    unsigned long *FATEntryValue;
    unsigned long FATEntrySector;
    unsigned char retry = 0;

    //get sector number of the cluster entry in the FAT
    FATEntrySector = _unusedSectors + _reservedSectorCount + ((clusterNumber * 4) / _bytesPerSector) ;

    //get the offset address in that sector number
    FATEntryOffset = (unsigned int) ((clusterNumber * 4) % _bytesPerSector);

    //read the sector into a buffer
    while(retry < 10)
    { 
        if(!SD_readSingleBlock(FATEntrySector)) break; retry++;
    }

    //get the cluster address from the buffer
    FATEntryValue = (unsigned long *) &_buffer[FATEntryOffset];

    if(get_set == GET)
      return ((*FATEntryValue) & 0x0fffffff);

    *FATEntryValue = clusterEntry;   //for setting new value in cluster entry in FAT

    SD_writeSingleBlock(FATEntrySector);

    return (0);
}

//********************************************************************************************
//Function: to get or set next free cluster or total free clusters in FSinfo sector of SD card
//Arguments: 1.flag:TOTAL_FREE or NEXT_FREE, 
//			 2.flag: GET or SET 
//			 3.new FS entry, when argument2 is SET; or 0, when argument2 is GET
//return: next free cluster, if arg1 is NEXT_FREE & arg2 is GET
//        total number of free clusters, if arg1 is TOTAL_FREE & arg2 is GET
//		  0xffffffff, if any error or if arg2 is SET
//********************************************************************************************
unsigned long getSetFreeCluster(unsigned char totOrNext, unsigned char get_set, unsigned long FSEntry)
{
    struct FSInfo_Structure *FS = (struct FSInfo_Structure *) &_buffer;
    unsigned char error;

    SD_readSingleBlock(_unusedSectors + 1);

    if((FS->leadSignature != 0x41615252) || (FS->structureSignature != 0x61417272) || (FS->trailSignature !=0xaa550000))
      return 0xffffffff;

     if(get_set == GET)
     {
       if(totOrNext == TOTAL_FREE)
          return(FS->freeClusterCount);
       else // when totOrNext = NEXT_FREE
          return(FS->nextFreeCluster);
     }
     else
     {
       if(totOrNext == TOTAL_FREE)
          FS->freeClusterCount = FSEntry;
       else // when totOrNext = NEXT_FREE
    	  FS->nextFreeCluster = FSEntry;
     
       error = SD_writeSingleBlock(_unusedSectors + 1);	//update FSinfo
     }
     return 0xffffffff;
}

unsigned char isLongFilename(unsigned char *fileName)
{
    if (strlen(fileName) > 11)
    {
        return 1;
    }
    
    return 0;
    // there are fewer than 11 characters
    // look for . character
}

unsigned char numCharsToCompare(unsigned char *fileName)
{
    unsigned char numChars = 0;
    while(fileName[numChars] != 0 && fileName[numChars] != '*')
    {
        numChars++;
    }
    return numChars;
}

struct dir_Structure* ListFilesIEEE(unsigned long firstCluster)
{
    struct dir_Structure *dir;
    unsigned char startline;
    unsigned char entry[32];
    unsigned int dir_start;
    unsigned int f;
    unsigned int file;
    int fname_length;
    
    dir_start = 0x041f;
    file = 0;
    
    // open the current directory
    openDirectory(firstCluster);
    
    do {
        // get next directory entry
        dir = getNextDirectoryEntry();
        
        if (dir == 0) // this is the end of the directory
        {
            // write ending bytes
            startline = 0;
            dir_start += 0x001e;
            
            entry[startline] = (unsigned char)(dir_start & 0x00ff);
            entry[startline+1] = (unsigned char)((dir_start & 0xff00) >> 8);
            entry[startline+2] = 0xff;
            entry[startline+3] = 0xff;
            sprintf(&entry[startline+4], "BLOCKS FREE.             ");
            entry[startline+29] = 0x00;
            entry[startline+30] = 0x00;
            entry[startline+31] = 0x00;
            
            for (f = 0; f < 32; f++)
            {
                if (f == 31)
                {
                    send_byte(entry[f], 1);
                }
                else
                {
                    send_byte(entry[f], 0);
                }
            }
            return 0;
        }
        else
        {
            if((dir->attrib != ATTR_DIRECTORY) && (dir->attrib != ATTR_VOLUME_ID))
            {
                dir_start += 0x0020;
                
                startline = 0;
                fname_length = 0;
                
                entry[startline] = (unsigned char)(dir_start & 0x00ff);
                entry[startline+1] = (unsigned char)((dir_start & 0xff00) >> 8);
                entry[startline+2] = file+1;
                entry[startline+3] = 0x00;
                entry[startline+4] = 0x20;
                entry[startline+5] = 0x20;
                entry[startline+6] = 0x22;
                
                fname_length = 0;
                for (f = 0; f < 8; f++)
                {
                    if (dir->name[f] == ' ')
                        break;
                    
                    entry[startline+7+f] = dir->name[f];
                    fname_length++;
                }
                
                entry[startline+7+fname_length] = 0x22;
                for (f = 0; f < (17 - fname_length); f++)
                {
                    entry[startline+7+fname_length+f+1] = ' ';
                }
                
                entry[startline+25] = dir->name[8];
                entry[startline+26] = dir->name[9];
                entry[startline+27] = dir->name[10];
                
                entry[startline+28] = ' ';
                entry[startline+29] = ' ';
                entry[startline+30] = ' ';
                entry[startline+31] = 0x00;
                file++;
                
                for (f = 0; f < 32; f++)
                {
                    send_byte(entry[f], 0);
                }
            }
        }
    }
    while (dir != 0);
}

/*
struct dir_Structure* ListFilesIEEE2 ()
{
    unsigned long cluster, sector, firstSector, firstCluster, nextCluster;
    struct dir_Structure *dir;
    struct dir_Longentry_Structure *longent;
    unsigned int i;
    unsigned int file, f;
    unsigned char j,k;
    unsigned char lentstr[32];
    unsigned char entry[32];
    unsigned char ord;
    unsigned char is_long_entry;
    unsigned int dir_start;
    unsigned char startline;
    unsigned char thisch;
    int fname_length;
    cluster = _rootCluster; //root cluster
    is_long_entry = 0;
    dir_start = 0x041f;
    file = 0;
    while(1)
    {
        firstSector = getFirstSector (cluster);
        
        for(sector = 0; sector < _sectorPerCluster; sector++)
        {
            SD_readSingleBlock (firstSector + sector);
            
            for(i=0; i<_bytesPerSector; i+=32)
            {
                dir = (struct dir_Structure *) &_buffer[i];
                
                if(dir->name[0] == EMPTY) //indicates end of the file list of the directory
                {
                    // write ending bytes
                    startline = 0;
                    dir_start += 0x001e;
                    
                    entry[startline] = (unsigned char)(dir_start & 0x00ff);
                    entry[startline+1] = (unsigned char)((dir_start & 0xff00) >> 8);
                    entry[startline+2] = 0xff;
                    entry[startline+3] = 0xff;
                    sprintf(&entry[startline+4], "BLOCKS FREE.             ");
                    entry[startline+29] = 0x00;
                    entry[startline+30] = 0x00;
                    entry[startline+31] = 0x00;
                    
                    for (f = 0; f < 32; f++)
                    {
                        if (f == 31)
                        {
                            send_byte(entry[f], 1);
                        }
                        else
                        {
                            send_byte(entry[f], 0);
                        }
                    }
                    return 0;
                }
                if((dir->name[0] != DELETED) && (dir->attrib != ATTR_LONG_NAME))
                {
                    if((dir->attrib != 0x10) && (dir->attrib != 0x08))
                    {
                        dir_start += 0x0020;
                        
                        startline = 0;
                        fname_length = 0;
                        
                        entry[startline] = (unsigned char)(dir_start & 0x00ff);
                        entry[startline+1] = (unsigned char)((dir_start & 0xff00) >> 8);
                        entry[startline+2] = file+1;
                        entry[startline+3] = 0x00;
                        entry[startline+4] = 0x20;
                        entry[startline+5] = 0x20;
                        entry[startline+6] = 0x22;
                        
                        
                        if (is_long_entry == 1)
                        {
                            while(lentstr[fname_length] != '.' && lentstr[fname_length] != 0 && fname_length < 17)
                            {
                                thisch = lentstr[fname_length];
                                if (thisch >= 'a' && thisch <= 'z')
                                {
                                    thisch -= 32;
                                }
                                entry[startline+7+fname_length] = thisch;
                                fname_length++;
                            }
                        }
                        else
                        {
                            fname_length = 0;
                            for (f = 0; f < 8; f++)
                            {
                                if (dir->name[f] == ' ')
                                    break;
                                
                                entry[startline+7+f] = dir->name[f];
                                fname_length++;
                            }
                        }
                        
                        entry[startline+7+fname_length] = 0x22;
                        for (f = 0; f < (17 - fname_length); f++)
                        {
                            entry[startline+7+fname_length+f+1] = ' ';
                        }
                        
                        //entry[startline+25] = 'P';
                        //entry[startline+26] = 'R';
                        //entry[startline+27] = 'G';
                        entry[startline+25] = dir->name[8];
                        entry[startline+26] = dir->name[9];
                        entry[startline+27] = dir->name[10];
                        
                        entry[startline+28] = ' ';
                        entry[startline+29] = ' ';
                        entry[startline+30] = ' ';
                        entry[startline+31] = 0x00;
                        file++;
                        
                        for (f = 0; f < 32; f++)
                        {
                            send_byte(entry[f], 0);
                        }
                    }
                    
                    // clear out the long entry string
                    if (is_long_entry == 1)
                    {
                        for (k = 0; k < 32; k++)
                        {
                            lentstr[k] = 0;
                        }
                        
                    }
                    is_long_entry = 0;
                }
                else if (dir->attrib == ATTR_LONG_NAME)
                {
                    is_long_entry = 1;
                    longent = (struct dir_Longentry_Structure *) &_buffer[i];
                    
                    ord = (longent->LDIR_Ord & 0x0F) - 1;
                    
                    for (k = 0; k < 5; k++)
                        lentstr[k+(13*ord)] = (unsigned char)longent->LDIR_Name1[k];
                    
                    for (k = 0; k < 6; k++)
                        lentstr[k+5+(13*ord)] = (unsigned char)longent->LDIR_Name2[k];
                    
                    for (k = 0; k < 2; k++)
                        lentstr[k+11+(13*ord)] = (unsigned char)longent->LDIR_Name3[k];
                }
            }
        }
        
        cluster = (getSetNextCluster (cluster, GET, 0));
        
        if(cluster > 0x0ffffff6)
            return 0;
        if(cluster == 0) 
        {
            //transmitString_F(PSTR("Error in getting cluster"));
            return 0;
        }
    }
    
    return 0;
}
*/
 
void openDirectory(unsigned long firstCluster)
{
    // store cluster
    _filePosition.startCluster = firstCluster;
    _filePosition.cluster = firstCluster;
    _filePosition.sectorIndex = 0;
    _filePosition.byteCounter = 0;
}

struct dir_Structure *getNextDirectoryEntry()
{
    unsigned long firstSector;
    struct dir_Structure *dir;
    while(1)
    {
        firstSector = getFirstSector(_filePosition.cluster);
        
        for (; _filePosition.sectorIndex < _sectorPerCluster; _filePosition.sectorIndex++)
        {
            SD_readSingleBlock(firstSector + _filePosition.sectorIndex);
            for (; _filePosition.byteCounter < _bytesPerSector; _filePosition.byteCounter += 32)
            {
                // get current directory entry
                dir = (struct dir_Structure *) &_buffer[_filePosition.byteCounter];
                
                if (dir->name[0] == EMPTY)
                {
                    return 0;
                }
                
                // this is a valid file entry
                if((dir->name[0] != DELETED) && (dir->attrib != ATTR_LONG_NAME))
                {
                    _filePosition.byteCounter += 32;
                    return dir;
                }
            }
            // done with this sector
            _filePosition.byteCounter = 0;
        }
        // done with this cluster
        _filePosition.sectorIndex = 0;
        _filePosition.cluster = getSetNextCluster(_filePosition.cluster, GET, 0);
        
        // last cluster on the card
        if (_filePosition.cluster > 0x0ffffff6)
        {
            return 0;
        }
        if (_filePosition.cluster == 0)
        {
            transmitString_F(PSTR("Error in getting cluster"));
            return 0;
        }
    }
}

struct dir_Structure* findFile (unsigned char *fileName, unsigned long firstCluster)
{
    unsigned long firstSector;
    unsigned long cluster;
    unsigned long sector;
    struct dir_Structure *dir;
    struct dir_Longentry_Structure *longent;
    unsigned char is_long_entry;
    unsigned char *ustr;
    unsigned char done_long_entry_check;
    unsigned char this_long_filename_length;
    unsigned char k;
    unsigned char ord;
    unsigned char cmp_length;
    unsigned char cmp_long_fname;
    int result;
    int b;
    
    cmp_long_fname = isLongFilename(fileName);
    cmp_length = numCharsToCompare(fileName);
    memset(_longEntryString, 0, MAX_FILENAME);
    is_long_entry = 0;
    
    // convert filename to uppercase
    fileName = strupr(fileName);
    cluster = firstCluster;
    while (1)
    {
        // first sector in the current cluster
        firstSector = getFirstSector(cluster);
        
        // loop through sectors in this cluster
        for(sector = 0; sector < _sectorPerCluster; sector++)
        {
            SD_readSingleBlock (firstSector + sector);
            for(b = 0; b < _bytesPerSector; b += 32)
            {
                dir = (struct dir_Structure *) &_buffer[b];
                
                if(dir->name[0] == EMPTY) //indicates end of the file list of the directory
                {
                    // file does not exist
                    return 0;
                }
                
                if((dir->name[0] != DELETED) && (dir->attrib != ATTR_LONG_NAME))
                {
                    transmitString(dir->name);
                    transmitString("\r\n");
                    
                    if (cmp_long_fname == 1)
                    {
                        if (is_long_entry == 1)
                        {
                            ustr = strupr((unsigned char *)_longEntryString);
                            result = strncmp(fileName, ustr, cmp_length);
                            if (result == 0)
                            {
                                // found the file, long entry match
                                return dir;
                            }
                        }
                    }
                    else
                    {
                        result = strncmp(fileName, dir->name, cmp_length);
                        if (result == 0)
                        {
                            // found the file, regular match
                            return dir;
                        }
                    }
                    
                    // clear out the long entry string
                    if (is_long_entry == 1)
                    {
                        memset(_longEntryString, 0, MAX_FILENAME);
                    }
                    is_long_entry = 0;
                }
                else if (dir->attrib == ATTR_DIRECTORY)
                {
                    //transmitString("this is a directory!\r\n");
                }
                else if (dir->attrib == ATTR_LONG_NAME)
                {
                    is_long_entry = 1;
                    
                    longent = (struct dir_Longentry_Structure *) &_buffer[b];
                    
                    ord = (longent->LDIR_Ord & 0x0F) - 1;
                    this_long_filename_length = (13*ord);
                    
                    for (k = 0; k < 5; k++)
                    {
                        _longEntryString[this_long_filename_length] = (unsigned char)longent->LDIR_Name1[k];
                        this_long_filename_length++;
                    }
                    
                    for (k = 0; k < 6; k++)
                    {
                        _longEntryString[this_long_filename_length] = (unsigned char)longent->LDIR_Name2[k];
                        this_long_filename_length++;
                    }
                
                    for (k = 0; k < 2; k++)
                    {
                        _longEntryString[this_long_filename_length] = (unsigned char)longent->LDIR_Name3[k];
                        this_long_filename_length++;
                    }
                    
                }
            }
        }
        
        cluster = getSetNextCluster(cluster, GET, 0);
        
        // last cluster on the card
        if (cluster > 0x0ffffff6)
        {
            return 0;
        }
        if (cluster == 0)
        {
            transmitString_F(PSTR("Error in getting cluster"));
            return 0;
        }
    }
}
 
//***************************************************************************
//Function: if flag=READ then to read file from SD card and send contents to UART 
//if flag=VERIFY then functions will verify whether a specified file is already existing
//Arguments: flag (READ or VERIFY) and pointer to the file name
//return: 0, if normal operation or flag is READ
//	      1, if file is already existing and flag = VERIFY
//		  2, if file name is incompatible
//***************************************************************************
/*
unsigned char readFile (unsigned char flag, unsigned char *fileName)
{
struct dir_Structure *dir;
unsigned long cluster, byteCounter = 0, firstSector;
unsigned int k;
unsigned char j, error;

error = convertFileName (fileName); //convert fileName into FAT format
if(error) return 2;

dir = findFiles (GET_FILE, fileName); //get the file location
if(dir == 0) 
  return (0);

if(flag == VERIFY) return (1);	//specified file name is already existing
}
*/

//***************************************************************************
//Function: to convert normal short file name into FAT format
//Arguments: pointer to the file name
//return: 0, if successful else 1.
//***************************************************************************
/*
unsigned char convertFileName (unsigned char *fileName)
{
unsigned char fileNameFAT[11];
unsigned char j, k;

for(j=0; j<12; j++)
if(fileName[j] == '.') break;

if(j>8)
{
    // this is a long filename
    // convert to lowercase
    k = 0;
    while (fileName[k] != 0)
    {
        if (fileName[k] >= 65 && fileName[k] <= 90)
        {
            fileName[k] += 32;
        }
        k++;
    }
    return 1;
}

for(k=0; k<j; k++) //setting file name
  fileNameFAT[k] = fileName[k];

for(k=j; k<=7; k++) //filling file name trail with blanks
  fileNameFAT[k] = ' ';

j++;
for(k=8; k<11; k++) //setting file extention
{
  if(fileName[j] != 0)
    fileNameFAT[k] = fileName[j++];
  else //filling extension trail with blanks
    while(k<11)
      fileNameFAT[k++] = ' ';
}

for(j=0; j<11; j++) //converting small letters to caps
  if((fileNameFAT[j] >= 0x61) && (fileNameFAT[j] <= 0x7a))
    fileNameFAT[j] -= 0x20;

for(j=0; j<11; j++)
  fileName[j] = fileNameFAT[j];
  
fileName[11] = 0x00;

return 0;
}
*/

unsigned long getFirstCluster(struct dir_Structure *dir)
{
    return (((unsigned long) dir->firstClusterHI) << 16) | dir->firstClusterLO;
}

unsigned char openFileForReading(unsigned char *fileName, unsigned long dirCluster)
{
    struct dir_Structure *dir;
    
    dir = findFile(fileName, dirCluster);
    if (dir == 0)
    {
        return 0;
    }
    
    _filePosition.fileSize = dir->fileSize;
    _filePosition.startCluster = getFirstCluster(dir);
    
    _filePosition.cluster = _filePosition.startCluster;
    _filePosition.byteCounter = 0;
    _filePosition.sectorIndex = 0;
    _filePosition.dirStartCluster = dirCluster;
    
    return 1;
}

unsigned int getNextFileBlock()
{
    unsigned long sector;
    unsigned char error;
    
    // if cluster has no more sectors, move to next cluster
    if (_filePosition.sectorIndex == _sectorPerCluster)
    {
        _filePosition.sectorIndex = 0;
        _filePosition.cluster = getSetNextCluster(_filePosition.cluster, GET, 0);
    }
    
    sector = getFirstSector(_filePosition.cluster) + _filePosition.sectorIndex;
    
    error = SD_readSingleBlock(sector);
    _filePosition.byteCounter += 512;
    _filePosition.sectorIndex++;
    
    if (_filePosition.byteCounter > _filePosition.fileSize)
    {
        return _filePosition.fileSize - (_filePosition.byteCounter - 512);
    }
    else
    {
        return 512;
    }
    
}

// open a new file for writing
void openFileForWriting(unsigned char *fileName, unsigned long dirCluster)
{
    unsigned long cluster;
    
    // use existing buffer for filename
    _filePosition.fileName = _longEntryString;
    memset(_filePosition.fileName, 0, MAX_FILENAME);
    strcpy(_filePosition.fileName, fileName);
    //memcpy(_filePosition.fileName, fileName, 11);
    
    // find the start cluster for this file
    cluster = getSetFreeCluster(NEXT_FREE, GET, 0);
    if (cluster > _totalClusters)
    {
        cluster = _rootCluster;
    }
    
    // set the start cluster with EOF
    cluster = searchNextFreeCluster(cluster);
    
    getSetNextCluster(cluster, SET, EOF);   //last cluster of the file, marked EOF
    
    _filePosition.startCluster = cluster;
    _filePosition.cluster = cluster;
    _filePosition.fileSize = 0;
    _filePosition.sectorIndex = 0;
    _filePosition.dirStartCluster = dirCluster;
}

void writeBufferToFile(unsigned int bytesToWrite)
{
    unsigned char error;
    unsigned long nextCluster;
    unsigned long sector;
    // write a block to current file
    
    /*
    transmitString("dircluster: ");
    transmitHex(LONG, _filePosition.dirStartCluster);
    transmitString("\r\n");
    */
     
    sector = getFirstSector(_filePosition.cluster) + _filePosition.sectorIndex;
    
    /*
    transmitString("sector: ");
    transmitHex(LONG, sector);
    transmitString("\r\n");
    */
     
    error = SD_writeSingleBlock(sector);
    _filePosition.fileSize += bytesToWrite;
    _filePosition.sectorIndex++;
    
    if (_filePosition.sectorIndex == _sectorPerCluster)
    {
        _filePosition.sectorIndex = 0;
        // get the next free cluster
        nextCluster = searchNextFreeCluster(_filePosition.cluster);
        // link the previous cluster
        getSetNextCluster(_filePosition.cluster, SET, nextCluster);
        // set the last cluster with EOF
        getSetNextCluster(nextCluster, SET, EOF);
        _filePosition.cluster = nextCluster;
    }
}

void closeFile()
{
    unsigned char fileCreatedFlag = 0;
    unsigned char sector, j;
    unsigned long prevCluster, firstSector, cluster;
    unsigned int firstClusterHigh, i;
    unsigned int firstClusterLow;
    struct dir_Structure *dir;
    unsigned char checkSum;
    unsigned char islongfilename;
    
    struct dir_Longentry_Structure *longent;
    
    unsigned char fname_len;
    unsigned char fname_remainder;
    unsigned char num_long_entries;
    unsigned char curr_fname_pos;
    unsigned char curr_long_entry;
    unsigned long fileNameLong[39];
    
    islongfilename = isLongFilename(_filePosition.fileName);
    if (islongfilename == 1)
    {
        makeShortFilename(_filePosition.fileName, _filePosition.shortFilename);
        checkSum = ChkSum(_filePosition.shortFilename);
        
        fname_len = strlen(_filePosition.fileName);
        fname_remainder = fname_len % 13;
        num_long_entries = ((fname_len - fname_remainder) / 13) + 1;
        
        curr_fname_pos = 0;
        for (j = 0; j < fname_len+1; j++)
        {
            fileNameLong[j] = _filePosition.fileName[j];
        }
        for (j = fname_len+1; j < 39; j++)
        {
            fileNameLong[j] = 0xffff;
        }
        
        curr_long_entry = num_long_entries;
    }
    
    // set next free cluster in FAT
    getSetFreeCluster (NEXT_FREE, SET, _filePosition.cluster); //update FSinfo next free cluster entry
    
    //prevCluster = _rootCluster;
    prevCluster = _filePosition.dirStartCluster;
    
    /*
    transmitString("prevcluster: ");
    transmitHex(LONG, prevCluster);
    transmitString("\r\n");
    */
     
    while(1)
    {
        firstSector = getFirstSector (prevCluster);
        
        for(sector = 0; sector < _sectorPerCluster; sector++)
        {
            
            SD_readSingleBlock (firstSector + sector);
            
            /*
            transmitString("currsector: ");
            transmitHex(LONG, firstSector+sector);
            transmitString("\r\n");
            */
             
            for( i = 0; i < _bytesPerSector; i += 32)
            {
                dir = (struct dir_Structure *) &_buffer[i];
                
                if(fileCreatedFlag)   //to mark last directory entry with 0x00 (empty) mark
                { 					  //indicating end of the directory file list
                    dir->name[0] = EMPTY;
                    SD_writeSingleBlock(firstSector + sector);
                    return;
                }
                
                if (islongfilename == 0)
                {
                    //if((dir->name[0] == EMPTY) || (dir->name[0] == DELETED))  //looking for an empty slot to enter file info
                    if (dir->name[0] == EMPTY)
                    {
                        /*
                        transmitString("dir byte: ");
                        transmitHex(INT, i);
                        transmitString("\r\n");
                        */
                         
                        memcpy(dir->name, _filePosition.shortFilename, 11);
                        
                        dir->attrib = ATTR_ARCHIVE;	//settting file attribute as 'archive'
                        dir->NTreserved = 0;			//always set to 0
                        dir->timeTenth = 0;			//always set to 0
                        dir->createTime = 0x9684;		//fixed time of creation
                        dir->createDate = 0x3a37;		//fixed date of creation
                        dir->lastAccessDate = 0x3a37;	//fixed date of last access
                        dir->writeTime = 0x9684;		//fixed time of last write
                        dir->writeDate = 0x3a37;		//fixed date of last write
                        
                        firstClusterHigh = (unsigned int) ((_filePosition.startCluster & 0xffff0000) >> 16 );
                        firstClusterLow = (unsigned int) ( _filePosition.startCluster & 0x0000ffff);
                        
                        dir->firstClusterHI = firstClusterHigh;
                        dir->firstClusterLO = firstClusterLow;
                        dir->fileSize = _filePosition.fileSize;
                        
                        SD_writeSingleBlock (firstSector + sector);
                        fileCreatedFlag = 1;
                        
                        //TX_NEWLINE;
                        //TX_NEWLINE;
                        transmitString_F(PSTR(" File Created!"));
                        
                        freeMemoryUpdate (REMOVE, _filePosition.fileSize); //updating free memory count in FSinfo sector
                        
                    }
                }
                else
                {
                    if (dir->name[0] == EMPTY)
                    {
                        // create long directory entry
                        
                        longent = (struct dir_Longentry_Structure *) &_buffer[i];
                        
                        // fill in the long entry fields
                        if (curr_long_entry == num_long_entries)
                        {
                            longent->LDIR_Ord = 0x40 | curr_long_entry;
                        }
                        else
                        {
                            longent->LDIR_Ord = curr_long_entry;
                        }
                        
                        curr_long_entry--;
                        curr_fname_pos = curr_long_entry * 13;
                        
                        longent->LDIR_Name1[0] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name1[1] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name1[2] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name1[3] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name1[4] = fileNameLong[curr_fname_pos++];
                        
                        longent->LDIR_Name2[0] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name2[1] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name2[2] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name2[3] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name2[4] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name2[5] = fileNameLong[curr_fname_pos++];
                        
                        longent->LDIR_Name3[0] = fileNameLong[curr_fname_pos++];
                        longent->LDIR_Name3[1] = fileNameLong[curr_fname_pos++];
                        
                        longent->LDIR_Attr = ATTR_LONG_NAME;
                        longent->LDIR_Type = 0;
                        longent->LDIR_Chksum = checkSum;
                        longent->LDIR_FstClusLO = 0;
                        
                        SD_writeSingleBlock (firstSector + sector);
                        
                        if (curr_long_entry == 0)
                        {
                            // copy the short filename
                            /*
                            for (j = 0; j < 11; j++)
                            {
                                fileName[j] = shortfilename[j];
                            }
                            */
                            islongfilename = 0;
                        }
                    }
                }
            }
        }
        
        cluster = getSetNextCluster (prevCluster, GET, 0);
        
        if(cluster > 0x0ffffff6)
        {
            if(cluster == EOF)   //this situation will come when total files in root is multiple of (32*_sectorPerCluster)
            {  
                cluster = searchNextFreeCluster(prevCluster); //find next cluster for root directory entries
                getSetNextCluster(prevCluster, SET, cluster); //link the new cluster of root to the previous cluster
                getSetNextCluster(cluster, SET, EOF);  //set the new cluster as end of the root directory
            } 
            
            else
            {	
                transmitString_F(PSTR("End of Cluster Chain")); 
                return;
            }
        }
        if(cluster == 0) {transmitString_F(PSTR("Error in getting cluster")); return;}
        
        prevCluster = cluster;
    }
}



/*
void openFile(unsigned char *fileName, unsigned long *startcluster)
{
    unsigned char j, data, error, fileCreatedFlag = 0, start = 0, appendFile = 0, sectorEndFlag = 0, sector;
    unsigned int i, firstClusterHigh, firstClusterLow;
    struct dir_Structure *dir;
    unsigned long cluster, nextCluster, prevCluster, firstSector, clusterCount, extraMemory;
    int filename_position = 0;

    j = readFile (VERIFY, fileName);

    if(j == 1) 
    {
        //transmitString("exists, deleting.");
        findFiles (DELETE, fileName);
        j = 0;
    }
    
    cluster = getSetFreeCluster (NEXT_FREE, GET, 0);
      if(cluster > _totalClusters)
         cluster = _rootCluster;

      cluster = searchNextFreeCluster(cluster);
       if(cluster == 0)
       {
          TX_NEWLINE;
          //transmitString_F(PSTR(" No free cluster!"));
          return;
       }
      getSetNextCluster(cluster, SET, EOF);   //last cluster of the file, marked EOF
       
      firstClusterHigh = (unsigned int) ((cluster & 0xffff0000) >> 16 );
      firstClusterLow = (unsigned int) ( cluster & 0x0000ffff);
      _fileSize = 0;
    
    _fileStartCluster = cluster;
}
*/

//************************************************************************************
//Function: to create a file in FAT32 format in the root directory if given 
//			file name does not exist; if the file already exists then append the data
//Arguments: pointer to the file name
//return: none
//************************************************************************************

/*
void writeFileFromIEEE (unsigned char *fileName, unsigned long StartCluster)
{
unsigned char j, data, error, fileCreatedFlag = 0, start = 0, appendFile = 0, sectorEndFlag = 0, sector;
unsigned int i, firstClusterHigh, firstClusterLow;
struct dir_Structure *dir;
struct dir_Longentry_Structure *longent;
unsigned long cluster, nextCluster, prevCluster, firstSector, clusterCount, extraMemory;
unsigned char rdchar, rdbus;
unsigned char islongfilename;
unsigned char shortfilename[11];
unsigned char checkSum;
unsigned char curr_long_entry;
unsigned char num_long_entries;
unsigned char fname_remainder;
unsigned char fname_len;
unsigned char curr_fname_pos;
unsigned char chars_in_long_entry;
unsigned long fileNameLong[39];
// check for long file name....
islongfilename = 0;
if (fileName[11] != 0)
{
    islongfilename = 1;
    
    // prepare short file name
    makeshortfilename(fileName, shortfilename);
    checkSum = ChkSum(shortfilename);
    curr_long_entry = 0;
    
    j = 11;
    while (fileName[j] != 0)
    {
        j++;
    }
    
    fname_len = j;
    fname_remainder = j % 13;
    num_long_entries = ((j - fname_remainder) / 13) + 1;
    transmitByte('0'+num_long_entries);
    curr_fname_pos = 0;
    
    for (j = 0; j < fname_len+1; j++)
    {
        fileNameLong[j] = fileName[j];
    }
    for (j = fname_len+1; j < 39; j++)
    {
        fileNameLong[j] = 0xffff;
    }
    
    curr_long_entry = num_long_entries;
}


// start writing file at StartCluster
cluster = StartCluster;

firstClusterHigh = (unsigned int) ((cluster & 0xffff0000) >> 16 );
firstClusterLow = (unsigned int) ( cluster & 0x0000ffff);

while(1)
{
   if(start)
   {
      start = 0;
      
      // get first block of cluster
	  startBlock = getFirstSector (cluster) + sector;
	  SD_readSingleBlock (startBlock);
	  i = _fileSize % _bytesPerSector;
	  j = sector;
   }
   else
   {
      startBlock = getFirstSector (cluster);
	  i=0;
	  j=0;
   }
   
   do
   {
     
     wait_for_dav_low();
     PORTC = NOT_NDAC & NOT_NRFD;
    // read byte
    //rdchar = PINA;
    recv_byte_IEEE(&rdchar);
    rdbus = PINC;
    
    //rdchar = ~rdchar;
    data = rdchar;

     buffer[i++] = data;
	 _fileSize++;
	 
     if(i >= 512)   //though 'i' will never become greater than 512, it's kept here to avoid 
	 {				//infinite loop in case it happens to be greater than 512 due to some data corruption
	   i=0;
	   error = SD_writeSingleBlock (startBlock);
       j++;
	   if(j == _sectorPerCluster)
       {
            j = 0;
            break;
        }
	   startBlock++; 
     }
     
     // raise NDAC
    PORTC = NOT_NRFD;
    wait_for_dav_high();
    PORTC = NOT_NDAC;
	}
    while((rdbus & EOI) != 0x00);
    
   if((rdbus & EOI) == 0x00)
   {
      error = SD_writeSingleBlock (startBlock);

      break;
   } 
	  
   prevCluster = cluster;

   transmitString_F(PSTR("getting a free cluster.."));
   TX_NEWLINE;

   cluster = searchNextFreeCluster(prevCluster); //look for a free cluster starting from the current cluster

   if(cluster == 0)
   {
      TX_NEWLINE;
      transmitString_F(PSTR(" No free cluster!"));
	  return;
   }

   getSetNextCluster(prevCluster, SET, cluster);
   getSetNextCluster(cluster, SET, EOF);   //last cluster of the file, marked EOF
   
   // raise NDAC
    PORTC = NOT_NRFD;
    wait_for_dav_high();
    PORTC = NOT_NDAC;
}        


getSetFreeCluster (NEXT_FREE, SET, cluster); //update FSinfo next free cluster entry

if(appendFile)  //executes this loop if file is to be appended
{
  SD_readSingleBlock (_appendFileSector);    
  dir = (struct dir_Structure *) &buffer[_appendFileLocation]; 
  extraMemory = _fileSize - dir->fileSize;
  dir->fileSize = _fileSize;
  SD_writeSingleBlock (_appendFileSector);
  freeMemoryUpdate (REMOVE, extraMemory); //updating free memory count in FSinfo sector;

  
  TX_NEWLINE;
  transmitString_F(PSTR(" File appended!"));
  TX_NEWLINE;
  return;
}

//executes following portion when new file is created

prevCluster = _rootCluster; //root cluster

while(1)
{
   firstSector = getFirstSector (prevCluster);

   for(sector = 0; sector < _sectorPerCluster; sector++)
   {
     SD_readSingleBlock (firstSector + sector);
	

     for(i=0; i<_bytesPerSector; i+=32)
     {
	    dir = (struct dir_Structure *) &buffer[i];

		if(fileCreatedFlag)   //to mark last directory entry with 0x00 (empty) mark
		 { 					  //indicating end of the directory file list
		   dir->name[0] = 0x00;
           return;
         }

        if (islongfilename == 0)
        {

            if((dir->name[0] == EMPTY) || (dir->name[0] == DELETED))  //looking for an empty slot to enter file info
            {
                for(j=0; j<11; j++)
                    dir->name[j] = fileName[j];
                
                dir->attrib = ATTR_ARCHIVE;	//settting file attribute as 'archive'
                dir->NTreserved = 0;			//always set to 0
                dir->timeTenth = 0;			//always set to 0
                dir->createTime = 0x9684;		//fixed time of creation
                dir->createDate = 0x3a37;		//fixed date of creation
                dir->lastAccessDate = 0x3a37;	//fixed date of last access
                dir->writeTime = 0x9684;		//fixed time of last write
                dir->writeDate = 0x3a37;		//fixed date of last write
                dir->firstClusterHI = firstClusterHigh;
                dir->firstClusterLO = firstClusterLow;
                dir->fileSize = _fileSize;

                SD_writeSingleBlock (firstSector + sector);
                fileCreatedFlag = 1;

                TX_NEWLINE;
                TX_NEWLINE;
                transmitString_F(PSTR(" File Created!"));

                freeMemoryUpdate (REMOVE, _fileSize); //updating free memory count in FSinfo sector
            }
        }
        else 
        {
            if (dir->name[0] == EMPTY)
            {
                //if (curr_long_entry == 0)
                //{
            
                    // create long directory entry
                    
                    //curr_long_entry++;
                    longent = (struct dir_Longentry_Structure *) &buffer[i];
                    
                    // fill in the long entry fields
                    if (curr_long_entry == num_long_entries)
                    {
                        longent->LDIR_Ord = 0x40 | curr_long_entry;
                    }
                    else 
                    {
                        longent->LDIR_Ord = curr_long_entry;
                    }
                    
                    curr_long_entry--;
                    curr_fname_pos = curr_long_entry * 13;
                    
                    longent->LDIR_Name1[0] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name1[1] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name1[2] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name1[3] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name1[4] = fileNameLong[curr_fname_pos++];
                    
                    longent->LDIR_Name2[0] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name2[1] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name2[2] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name2[3] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name2[4] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name2[5] = fileNameLong[curr_fname_pos++];
                    
                    longent->LDIR_Name3[0] = fileNameLong[curr_fname_pos++];
                    longent->LDIR_Name3[1] = fileNameLong[curr_fname_pos++];
                    
                    longent->LDIR_Attr = ATTR_LONG_NAME;
                    longent->LDIR_Type = 0;
                    longent->LDIR_Chksum = checkSum;
                    longent->LDIR_FstClusLO = 0;
                    
                    SD_writeSingleBlock (firstSector + sector);
                    
                    if (curr_long_entry == 0)
                    {
                        // copy the short filename
                        for (j = 0; j < 11; j++)
                        {
                            fileName[j] = shortfilename[j];
                        }
                        islongfilename = 0;
                    }
                //}
            }

        }
     }
   }

   cluster = getSetNextCluster (prevCluster, GET, 0);

   if(cluster > 0x0ffffff6)
   {
      if(cluster == EOF)   //this situation will come when total files in root is multiple of (32*_sectorPerCluster)
	  {  
		cluster = searchNextFreeCluster(prevCluster); //find next cluster for root directory entries
		getSetNextCluster(prevCluster, SET, cluster); //link the new cluster of root to the previous cluster
		getSetNextCluster(cluster, SET, EOF);  //set the new cluster as end of the root directory
      } 

      else
      {	
	    transmitString_F(PSTR("End of Cluster Chain")); 
	    return;
      }
   }
   if(cluster == 0) {transmitString_F(PSTR("Error in getting cluster")); return;}
   
   prevCluster = cluster;
 }
 
 return;
}
*/

//***************************************************************************
//Function: to search for the next free cluster in the root directory
//          starting from a specified cluster
//Arguments: Starting cluster
//return: the next free cluster
//****************************************************************
unsigned long searchNextFreeCluster (unsigned long startCluster)
{
  unsigned long cluster, *value, sector;
  unsigned char i;
    
	startCluster -=  (startCluster % 128);   //to start with the first file in a FAT sector
    for(cluster =startCluster; cluster <_totalClusters; cluster+=128) 
    {
      sector = _unusedSectors + _reservedSectorCount + ((cluster * 4) / _bytesPerSector);
        transmitString("sector: ");
        transmitHex(LONG, sector);
        transmitString("\r\n");
      SD_readSingleBlock(sector);
      for(i=0; i<128; i++)
      {
       	 value = (unsigned long *) &_buffer[i*4];
         if(((*value) & 0x0fffffff) == 0)
            return(cluster+i);
      }  
    } 

    transmitString("no free sectors\r\n");
 return 0;
}

//********************************************************************
//Function: to delete a specified file from the root directory
//Arguments: pointer to the file name
//return: none
//********************************************************************
/*
void deleteFile (unsigned char *fileName)
{
  unsigned char error;

  error = convertFileName (fileName);
  if(error) return;

  findFiles (DELETE, fileName);
}
*/

//********************************************************************
//Function: update the free memory count in the FSinfo sector. 
//			Whenever a file is deleted or created, this function will be called
//			to ADD or REMOVE clusters occupied by the file
//Arguments: #1.flag ADD or REMOVE #2.file size in Bytes
//return: none
//********************************************************************
void freeMemoryUpdate (unsigned char flag, unsigned long size)
{
  unsigned long freeClusters;
  //convert file size into number of clusters occupied
  if((size % 512) == 0) size = size / 512;
  else size = (size / 512) +1;
  if((size % 8) == 0) size = size / 8;
  else size = (size / 8) +1;

  if(_freeClusterCountUpdated)
  {
	freeClusters = getSetFreeCluster (TOTAL_FREE, GET, 0);
	if(flag == ADD)
  	   freeClusters = freeClusters + size;
	else  //when flag = REMOVE
	   freeClusters = freeClusters - size;
	getSetFreeCluster (TOTAL_FREE, SET, freeClusters);
  }
}

void makeShortFilename(unsigned char *longFilename, unsigned char *shortFilename)
{
    // make a short file name from the given long file name
    int i;
    unsigned char thechar;
    for (i = 0; i < 6; i++)
    {
        thechar = longFilename[i];
        
        if (longFilename[i] >= 'a' && longFilename[i] <= 'z')
        {
            thechar = longFilename[i] - 32;
        }
        
        if (thechar < 'A' || thechar > 'Z')
        {
            thechar = '_';
        }
        
        shortFilename[i] = thechar;
    }
     
    shortFilename[6] = '~';
    shortFilename[7] = '1';
    shortFilename[8] = 'P';
    shortFilename[9] = 'R';
    shortFilename[10] = 'G';
}


//-----------------------------------------------------------------------------
//	ChkSum()
//	Returns an unsigned byte checksum computed on an unsigned byte
//	array.  The array must be 11 bytes long and is assumed to contain
//	a name stored in the format of a MS-DOS directory entry.
//	Passed:	 pFcbName    Pointer to an unsigned byte array assumed to be
//                          11 bytes long.
//	Returns: Sum         An 8-bit unsigned checksum of the array pointed
//                           to by pFcbName.
//------------------------------------------------------------------------------
unsigned char ChkSum (unsigned char *pFcbName)
{
    int FcbNameLen;
    unsigned char Sum;

    Sum = 0;
    for (FcbNameLen=11; FcbNameLen!=0; FcbNameLen--) {
        // NOTE: The operation is an unsigned char rotate right
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
}


