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
    //unsigned char filenameLength = strlen(fileName);
    unsigned char filenameLength = 0;
    unsigned char i;
    while (fileName[filenameLength] != 0)
    {
        filenameLength++;
    }
    
    // if file is longer than 12 characters, not possible to be short filename
    if (filenameLength > 12)
    {
        return 1;
    }
    
    // if filename > 8, it has an extension if it's a short filename
    if (filenameLength > 8)
    {
        if (fileName[filenameLength-4] != '.')
        {
            // no extension or extension is an odd length
            // this is a long filename
            return 1;
        }
    }
    
    // check if it contains a space
    for (i = 0; i < filenameLength; i++)
    {
        //transmitString("checking: ");
        //transmitByte(fileName[i]);
        //TX_NEWLINE;
        if (fileName[i] == ' ')
        {
            // short filenames cannot have spaces
            return 1;
        }
    }
    
    
    return 0;
}

unsigned char numCharsToCompare(unsigned char *fileName, unsigned char maxChars)
{
    unsigned char numChars = 0;
    while(numChars < maxChars && fileName[numChars] != 0 && fileName[numChars] != '*')
    {
        numChars++;
    }
    return numChars;
}

void openDirectory(unsigned long firstCluster)
{
    // store cluster
    _filePosition.startCluster = firstCluster;
    _filePosition.cluster = firstCluster;
    _filePosition.sectorIndex = 0;
    _filePosition.byteCounter = 0;
}

void deleteFile()
{
    unsigned long sector;
    unsigned long byte;
    struct dir_Structure *dir;
    
    sector = getFirstSector(_filePosition.cluster) + _filePosition.sectorIndex;
    SD_readSingleBlock(sector);
    
    byte = _filePosition.byteCounter-32;
    dir = (struct dir_Structure *) &_buffer[byte];
    dir->name[0] = EMPTY;
    SD_writeSingleBlock(sector);
}

struct dir_Structure *getNextDirectoryEntry()
{
    unsigned long firstSector;
    struct dir_Structure *dir;
    struct dir_Longentry_Structure *longent;
    unsigned char ord;
    unsigned char this_long_filename_length;
    unsigned char k;
    
    // reset long entry info
    memset(_longEntryString, 0, MAX_FILENAME);
    _filePosition.isLongFilename = 0;
    _filePosition.fileName = _longEntryString;
    
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
                else if (dir->attrib == ATTR_LONG_NAME)
                {
                    _filePosition.isLongFilename = 1;
                    
                    longent = (struct dir_Longentry_Structure *) &_buffer[_filePosition.byteCounter];
                    
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

void convertToShortFilename(unsigned char *input, unsigned char *output)
{
    unsigned char extPos;
    unsigned char inputLen;
    
    /*
    while (input[inputLen] != 0)
    {
        inputLen++;
    }
    */
    
    inputLen = strlen(input);
     
    // set empty chars to space
    memset(output, ' ', 11);
    
    extPos = 0;
    if (inputLen >= 5)
    {
        if (input[inputLen-4] == '.')
        {
            extPos = inputLen-4;
        }
    }
    
    if (extPos > 0)
    {
        memcpy(output, input, extPos);
        memcpy(&output[8], &input[extPos+1], 3);
    }
    else
    {
        memcpy(output, input, inputLen);
    }
}

struct dir_Structure* findFile (unsigned char *fileName, unsigned long firstCluster)
{
    struct dir_Structure *dir;
    unsigned char cmp_long_fname;
    unsigned char cmp_length;
    unsigned char *ustr;
    unsigned char *findFileStr;
    unsigned char maxChars;
    int result;
    
    cmp_long_fname = isLongFilename(fileName);
    
    if (cmp_long_fname == 1)
    {
        fileName = strupr(fileName);
        findFileStr = fileName;
        maxChars = 32;
    }
    else
    {
        findFileStr = _filePosition.shortFilename;
        convertToShortFilename(fileName, findFileStr);
        maxChars = 11;
    }
    
    cmp_length = numCharsToCompare(findFileStr, maxChars);
    openDirectory(firstCluster);
    
    /*
    transmitString(findFileStr);
    TX_NEWLINE;
    transmitHex(CHAR, cmp_long_fname);
    TX_NEWLINE;
    transmitHex(CHAR, cmp_length);
    TX_NEWLINE;
    */
     
    do
    {
        dir = getNextDirectoryEntry();
        
        if (dir == 0)
        {
            // file not found
            return 0;
        }
        
        if (cmp_long_fname == 1)
        {
            if (_filePosition.isLongFilename == 1)
            {
                ustr = strupr((unsigned char *)_filePosition.fileName);
                result = strncmp(findFileStr, ustr, cmp_length);
                
                /*
                transmitString(ustr);
                TX_NEWLINE;
                transmitHex(INT, result);
                TX_NEWLINE;
                */
                 
                if (result == 0)
                {
                    return dir;
                }
            }
        }
        else
        {
            //transmitString(dir->name);
            //TX_NEWLINE;
            
            result = strncmp(findFileStr, dir->name, cmp_length);
            
            //transmitHex(INT, result);
            //TX_NEWLINE;
            
            if (result == 0)
            {
                return dir;
            }
        }
        
    }
    while (dir != 0);
    
    // if we get here, we haven't found the file
    return 0;
}

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
    unsigned char i;
    
    // use existing buffer for filename
    _filePosition.fileName = _longEntryString;
    memset(_filePosition.fileName, 0, MAX_FILENAME);
    //strcpy(_filePosition.fileName, fileName);
    
    i = 0;
    while (fileName[i] != 0)
    {
        transmitHex(CHAR, i);
        _filePosition.fileName[i] = fileName[i];
        i++;
    }

    memset(_filePosition.shortFilename, 0, 11);
    
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
    sector = getFirstSector(_filePosition.cluster) + _filePosition.sectorIndex;
    
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

/*
void printFileInfo()
{
    //transmitString("filename: ");
    
    TX_NEWLINE;
    TX_NEWLINE;
    transmitString(_filePosition.fileName);
    TX_NEWLINE;
    
    //transmitString("startCluster: ");
    transmitHex(LONG, _filePosition.startCluster);
    TX_NEWLINE;
    
    //transmitString("cluster: ");
    transmitHex(LONG, _filePosition.cluster);
    TX_NEWLINE;
    
    //transmitString("dirStartCluster: ");
    transmitHex(LONG, _filePosition.dirStartCluster);
    TX_NEWLINE;
    
    //transmitString("sectorIndex: ");
    transmitHex(CHAR, _filePosition.sectorIndex);
    TX_NEWLINE;
    
    //transmitString("fileSize: ");
    transmitHex(LONG, _filePosition.fileSize);
    TX_NEWLINE;
    
    //transmitString("shortfilename: ");
    transmitString(_filePosition.shortFilename);
    TX_NEWLINE;
    
    //transmitString("_rootCluster: ");
    transmitHex(LONG, _rootCluster);
    TX_NEWLINE;
}
*/

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
     
    // TEST
    islongfilename = isLongFilename(_filePosition.fileName);
    transmitHex(CHAR, islongfilename);
    
    if (islongfilename == 1)
    {
        //fileNameLong = _fileNameLong;
        memset(_filePosition.shortFilename, ' ', 11);
        makeShortFilename(_filePosition.fileName, _filePosition.shortFilename);
        checkSum = ChkSum(_filePosition.shortFilename);
        
        fname_len = strlen(_filePosition.fileName);
        fname_remainder = fname_len % 13;
        num_long_entries = ((fname_len - fname_remainder) / 13) + 1;
        
        curr_long_entry = num_long_entries;
    }
    else
    {
        // make short filename into FAT format
        convertToShortFilename(_filePosition.fileName, _filePosition.shortFilename);
    }
    
    // set next free cluster in FAT
    getSetFreeCluster (NEXT_FREE, SET, _filePosition.cluster); //update FSinfo next free cluster entry
    
    prevCluster = _filePosition.dirStartCluster;
    
    while(1)
    {
        firstSector = getFirstSector (prevCluster);
        
        for(sector = 0; sector < _sectorPerCluster; sector++)
        {
            SD_readSingleBlock (firstSector + sector);
            
            for( i = 0; i < _bytesPerSector; i += 32)
            {
                dir = (struct dir_Structure *) &_buffer[i];
                
                if(fileCreatedFlag)   //to mark last directory entry with 0x00 (empty) mark
                { 					  //indicating end of the directory file list
                    dir->name[0] = EMPTY;
                    SD_writeSingleBlock(firstSector + sector);
                    
                    freeMemoryUpdate (REMOVE, _filePosition.fileSize); //updating free memory count in FSinfo sector
                    return;
                }
                
                if (islongfilename == 0)
                {
                    if (dir->name[0] == EMPTY)
                    {
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
                        
                        transmitString_F(PSTR(" File Created!"));
                    }
                }
                else
                {
                    if (dir->name[0] == EMPTY)
                    {
                        // create long directory entry
                        longent = (struct dir_Longentry_Structure *) &_buffer[i];
                        memset(longent, 0xff, 32);
                        
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
                        
                        j = 0;
                        while (curr_fname_pos < fname_len && j < 5)
                        {
                            longent->LDIR_Name1[j++] = _filePosition.fileName[curr_fname_pos++];
                        }
                        
                        j = 0;
                        while (curr_fname_pos < fname_len && j < 6)
                        {
                            longent->LDIR_Name2[j++] = _filePosition.fileName[curr_fname_pos++];
                        }
                        
                        j = 0;
                        while (curr_fname_pos < fname_len && j < 2)
                        {
                            longent->LDIR_Name3[j++] = _filePosition.fileName[curr_fname_pos++];
                        }
                        
                        longent->LDIR_Attr = ATTR_LONG_NAME;
                        longent->LDIR_Type = 0;
                        longent->LDIR_Chksum = checkSum;
                        longent->LDIR_FstClusLO = 0;
                        
                        SD_writeSingleBlock (firstSector + sector);
                        
                        // if there are no long entries remaining, set a flag so the next entry is the FAT short entry
                        if (curr_long_entry == 0)
                        {
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
  if ((size % 512) == 0)
      size = size / 512;
  else
      size = (size / 512) +1;
    
  if ((size % 8) == 0)
      size = size / 8;
  else
      size = (size / 8) +1;

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


