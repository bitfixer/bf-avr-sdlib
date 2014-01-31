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

void startFileRead(struct dir_Structure *dir, file_stat *thisFileStat)
{
    thisFileStat->currentCluster = (((unsigned long) dir->firstClusterHI) << 16) | dir->firstClusterLO;
    thisFileStat->fileSize = dir->fileSize;
    thisFileStat->byteCounter = 0;
    thisFileStat->currentSector = getFirstSector(thisFileStat->currentCluster);
    thisFileStat->sectorIndex = 0;
}

void getCurrentFileBlock(file_stat *thisFileStat)
{
    unsigned long nextBlockAddr;
    nextBlockAddr = getNextBlockAddress(thisFileStat);
    
    SD_readSingleBlock(nextBlockAddr);
}

unsigned long getNextBlockAddress(file_stat *thisFileStat)
{
    unsigned long nextAddress;
    
    nextAddress = thisFileStat->currentSector;
    thisFileStat->sectorIndex++;
    thisFileStat->currentSector++;
    thisFileStat->byteCounter += 512;
    
    if (thisFileStat->sectorIndex >= _sectorPerCluster)
    {
        // go to next cluster and reset counter
        thisFileStat->currentCluster = getSetNextCluster(thisFileStat->currentCluster, GET, 0);
        thisFileStat->currentSector = getFirstSector(thisFileStat->currentCluster);
        thisFileStat->sectorIndex = 0;
    }
    
    return nextAddress;
}

struct dir_Structure* findFiles2 (unsigned char flag, unsigned char *fileName, unsigned char cmp_long_fname, unsigned long firstCluster)
{
    unsigned long firstSector;
    unsigned long cluster;
    unsigned long sector;
    struct dir_Structure *dir;
    struct dir_Longentry_Structure *longent;
    unsigned char is_long_entry;
    unsigned char is_long_entry_match;
    unsigned char lentstr[32];
    unsigned char done_long_entry_check;
    unsigned char this_long_filename_length;
    unsigned char k;
    unsigned char ord;
    
    int b;
    
    memset(lentstr, 0, 32);
    
    is_long_entry = 0;
    is_long_entry_match = 1;
    
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
                    
                    // clear out the long entry string
                    if (is_long_entry == 1)
                    {
                        transmitString(lentstr);
                        transmitString("\r\n");
                        memset(lentstr, 0, 32);
                    }
                    is_long_entry = 0;
                    is_long_entry_match = 1;
                    
                    
                }
                else if (dir->attrib == ATTR_LONG_NAME)
                {
                    is_long_entry = 1;
                    if (is_long_entry_match == 1)
                    {
                        done_long_entry_check = 0;
                        
                        longent = (struct dir_Longentry_Structure *) &_buffer[b];
                        
                        ord = (longent->LDIR_Ord & 0x0F) - 1;
                        this_long_filename_length = (13*ord);
                        
                        for (k = 0; k < 5; k++)
                        {
                            lentstr[this_long_filename_length] = (unsigned char)longent->LDIR_Name1[k];
                            this_long_filename_length++;
                        }
                        
                        if (is_long_entry_match == 1 && done_long_entry_check == 0)
                        {
                            for (k = 0; k < 6; k++)
                            {
                                lentstr[this_long_filename_length] = (unsigned char)longent->LDIR_Name2[k];
                                this_long_filename_length++;
                            }
                        }
                        
                        if (is_long_entry_match == 1 && done_long_entry_check == 0)
                        {
                            for (k = 0; k < 2; k++)
                            {
                                lentstr[this_long_filename_length] = (unsigned char)longent->LDIR_Name3[k];
                                this_long_filename_length++;
                            }
                        }
                        
                    }
                }
                
            }
        }
        
        
        cluster = (getSetNextCluster (cluster, GET, 0));
        
        if(cluster > 0x0ffffff6)
        {
            return 0;
        }
        if(cluster == 0)
        {
            transmitString_F(PSTR("Error in getting cluster"));
            return 0;
        }
    }
}

struct dir_Structure* findFilesL (unsigned char flag, unsigned char *fileName, unsigned char cmp_long_fname)
{
unsigned long cluster, sector, firstSector, firstCluster, nextCluster;
struct dir_Structure *dir;
struct dir_Longentry_Structure *longent;
unsigned int i;
unsigned char j,k,index;
unsigned char tf, this_fname_len;
unsigned char lentstr[32];
unsigned char ord;
unsigned char is_long_entry;
unsigned char is_long_entry_match;
unsigned char done_long_entry_check;
unsigned char fname_len;
unsigned char wildcard;
unsigned char temp, temp2;

cluster = _rootCluster; //root cluster
    
is_long_entry = 0;
for (i = 0; i < 32; i++)
{
    lentstr[i] = 0;
}
is_long_entry_match = 1;

while(1)
{
    // get first sector on this card
    firstSector = getFirstSector (cluster);

    // loop through sectors in this cluster
    for(sector = 0; sector < _sectorPerCluster; sector++)
    {
        SD_readSingleBlock (firstSector + sector);
        for(i = 0; i < _bytesPerSector; i += 32)
        {
	    dir = (struct dir_Structure *) &_buffer[i];
            transmitString(dir->name);
            
        if(dir->name[0] == EMPTY) //indicates end of the file list of the directory
		{
          // file does not exist
		  return 0;   
		}
		if((dir->name[0] != DELETED) && (dir->attrib != ATTR_LONG_NAME))
        {
            if((flag == GET_FILE) || (flag == DELETE))
            {
                j = 0;

                if (cmp_long_fname == 1)
                {
                    // check long file name entry for exact match
                    j = 0;
                    
                    this_fname_len = index;
                    if (is_long_entry == 1)
                    {
                        // do long entry check
                        j = 11;
                        for (tf = 0; tf < fname_len; tf++)
                        {
                            temp = lentstr[tf];
                            temp2 = fileName[tf];
                            
                            // set characters to uppercase
                            if (temp >= 'a' && temp <= 'z')
                            {
                                temp -= 32;
                            }
                            
                            if (temp2 >= 'a' && temp2 <= 'z')
                            {
                                temp2 -= 32;
                            }
                            
                            //if (lentstr[tf] != fileName[tf])
                            if (temp != temp2)
                            {
                                // check for wildcard
                                if (fileName[tf] == '*')
                                {
                                    // what's after doesn't matter, this is a match
                                    j = 11;
                                    break;
                                }
                            
                                // this is not a match
                                j = 0;
                                break;
                            }
                        }
                    }
                }
                else 
                {
                    for(j=0; j<11; j++)
                    {
                        if(dir->name[j] != fileName[j]) 
                        {
                            break;
                        }
                    }
                }

                if(j == 11)
        		{
                // found the right file, proceed.

                if(flag == GET_FILE)
                {
                    _appendFileSector = firstSector + sector;
                    _appendFileLocation = i;
                    _appendStartCluster = (((unsigned long) dir->firstClusterHI) << 16) | dir->firstClusterLO;
                    _fileSize = dir->fileSize;
                    return (dir);
                }
                else    //when flag = DELETE
                {
                    firstCluster = (((unsigned long) dir->firstClusterHI) << 16) | dir->firstClusterLO;

                    //mark file as 'deleted' in FAT table
                    dir->name[0] = DELETED;    
                    SD_writeSingleBlock (firstSector+sector);

                    freeMemoryUpdate (ADD, dir->fileSize);

                    //update next free cluster entry in FSinfo sector
                    cluster = getSetFreeCluster (NEXT_FREE, GET, 0); 
                    if(firstCluster < cluster)
                        getSetFreeCluster (NEXT_FREE, SET, firstCluster);

                    //mark all the clusters allocated to the file as 'free'
                    while(1)  
                    {
                        nextCluster = getSetNextCluster (firstCluster, GET, 0);
                        getSetNextCluster (firstCluster, SET, 0);
                        if(nextCluster > 0x0ffffff6) 
                        {
                            return 0;
                        }
                        firstCluster = nextCluster;
                    } 
                }
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
          is_long_entry_match = 1;
       }
       // handle long file name entries
       else if (dir->attrib == ATTR_LONG_NAME)
       {
            is_long_entry = 1;
            if (is_long_entry_match == 1)
            {
                done_long_entry_check = 0;
                
                longent = (struct dir_Longentry_Structure *) &_buffer[i];
                
                ord = (longent->LDIR_Ord & 0x0F) - 1;
                index = (13*ord);
                
                for (k = 0; k < 5; k++)
                {
                    lentstr[index] = (unsigned char)longent->LDIR_Name1[k];
                    index++;
                }
                 
                if (is_long_entry_match == 1 && done_long_entry_check == 0)
                {
                    for (k = 0; k < 6; k++)
                    {
                        lentstr[index] = (unsigned char)longent->LDIR_Name2[k];
                        index++;
                    }
                }
                   
                if (is_long_entry_match == 1 && done_long_entry_check == 0)
                {
                    for (k = 0; k < 2; k++)
                    {
                        lentstr[index] = (unsigned char)longent->LDIR_Name3[k];
                        index++;
                    }
                }
                
            }
       }
     }
   }

   cluster = (getSetNextCluster (cluster, GET, 0));

   if(cluster > 0x0ffffff6)
   	 return 0;
   if(cluster == 0) 
   {transmitString_F(PSTR("Error in getting cluster"));  return 0;}
 }
return 0;
}

struct dir_Structure* findFiles (unsigned char flag, unsigned char *fileName)
{
    return findFilesL(flag,fileName,0);
}
//***************************************************************************
//Function: if flag=READ then to read file from SD card and send contents to UART 
//if flag=VERIFY then functions will verify whether a specified file is already existing
//Arguments: flag (READ or VERIFY) and pointer to the file name
//return: 0, if normal operation or flag is READ
//	      1, if file is already existing and flag = VERIFY
//		  2, if file name is incompatible
//***************************************************************************
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

//***************************************************************************
//Function: to convert normal short file name into FAT format
//Arguments: pointer to the file name
//return: 0, if successful else 1.
//***************************************************************************
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
//{transmitString_F(PSTR("Invalid fileName..")); return 1;}

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

//************************************************************************************
//Function: to create a file in FAT32 format in the root directory if given 
//			file name does not exist; if the file already exists then append the data
//Arguments: pointer to the file name
//return: none
//************************************************************************************
/*
void writeFile (unsigned char *fileName)
{
unsigned char j, data, error, fileCreatedFlag = 0, start = 0, appendFile = 0, sectorEndFlag = 0, sector;
unsigned int i, firstClusterHigh, firstClusterLow;
struct dir_Structure *dir;
unsigned long cluster, nextCluster, prevCluster, firstSector, clusterCount, extraMemory;

j = readFile (VERIFY, fileName);

if(j == 1) 
{
  transmitString_F(PSTR("  File already existing, appending data..")); 
  appendFile = 1;
  cluster = _appendStartCluster;
  clusterCount=0;
  while(1)
  {
    nextCluster = getSetNextCluster (cluster, GET, 0);
    if(nextCluster == EOF) break;
	cluster = nextCluster;
	clusterCount++;
  }

  sector = (_fileSize - (clusterCount * _sectorPerCluster * _bytesPerSector)) / _bytesPerSector; //last sector number of the last cluster of the file
  start = 1;
//  appendFile();
//  return;
}
else if(j == 2) 
   return; //invalid file name
else
{
  TX_NEWLINE;
  transmitString_F(PSTR(" Creating File.."));

  cluster = getSetFreeCluster (NEXT_FREE, GET, 0);
  if(cluster > _totalClusters)
     cluster = _rootCluster;

  cluster = searchNextFreeCluster(cluster);
   if(cluster == 0)
   {
      TX_NEWLINE;
      transmitString_F(PSTR(" No free cluster!"));
	  return;
   }
  getSetNextCluster(cluster, SET, EOF);   //last cluster of the file, marked EOF
   
  firstClusterHigh = (unsigned int) ((cluster & 0xffff0000) >> 16 );
  firstClusterLow = (unsigned int) ( cluster & 0x0000ffff);
  _fileSize = 0;
}

transmitString_F(PSTR("first cluster is: "));
transmitHex(LONG, cluster);
TX_NEWLINE;

while(1)
{
   if(start)
   {
      start = 0;
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
   

   TX_NEWLINE;
   transmitString_F(PSTR(" Enter text (end with ~):"));
   unsigned char testdata[6];
   
   testdata[0] = 'h';
   testdata[1] = 'e';
   testdata[2] = 'l';
   testdata[3] = 'l';
   testdata[4] = 'o';
   testdata[4] = '~';
   
   do
   {
     if(sectorEndFlag == 1) //special case when the last character in previous sector was '\r'
	 {
	 	transmitByte ('\n');
        buffer[i++] = '\n'; //appending 'Line Feed (LF)' character
		_fileSize++;
	 }

	sectorEndFlag = 0;

	 //data = receiveByte();
     data = testdata[i];
	 if(data == 0x08)	//'Back Space' key pressed
	 { 
	   if(i != 0)
	   { 
	     transmitByte(data);
		 transmitByte(' '); 
	     transmitByte(data); 
	     i--; 
		 _fileSize--;
	   } 
	   continue;     
	 }
	 transmitByte(data);
     buffer[i++] = data;
	 _fileSize++;
     if(data == '\r')  //'Carriege Return (CR)' character
     {
        if(i == 512)
		   sectorEndFlag = 1;  //flag to indicate that the appended '\n' char should be put in the next sector
	    else
		{ 
		   transmitByte ('\n');
           buffer[i++] = '\n'; //appending 'Line Feed (LF)' character
		   _fileSize++;
	    }
     }
	 
     if(i >= 512)   //though 'i' will never become greater than 512, it's kept here to avoid 
	 {				//infinite loop in case it happens to be greater than 512 due to some data corruption
	   i=0;
	   error = SD_writeSingleBlock (startBlock);
       j++;
	   if(j == _sectorPerCluster) {j = 0; break;}
	   startBlock++; 
     }
	}while (data != '~');

   if(data == '~') 
   {
      _fileSize--;	//to remove the last entered '~' character
	  i--;
      
	  for(;i<512;i++)  //fill the rest of the buffer with 0x00
        buffer[i]= 0x00;
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
}        

transmitString_F(PSTR("setting free cluster."));
transmitString_F(PSTR("cluster is: "));
transmitHex(LONG, cluster);
TX_NEWLINE;

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
      SD_readSingleBlock(sector);
      for(i=0; i<128; i++)
      {
       	 value = (unsigned long *) &_buffer[i*4];
         if(((*value) & 0x0fffffff) == 0)
            return(cluster+i);
      }  
    } 

 return 0;
}

//***************************************************************************
//Function: to display total memory and free memory of SD card, using UART
//Arguments: none
//return: none
//Note: this routine can take upto 15sec for 1GB card (@1MHz clock)
//it tries to read from SD whether a free cluster count is stored, if it is stored
//then it will return immediately. Otherwise it will count the total number of
//free clusters, which takes time
//****************************************************************************
/*
void memoryStatistics (void)
{
unsigned long freeClusters, totalClusterCount, cluster;
unsigned long totalMemory, freeMemory;
unsigned long sector, *value;
unsigned int i;


totalMemory = _totalClusters * _sectorPerCluster / 1024;
totalMemory *= _bytesPerSector;

TX_NEWLINE;
TX_NEWLINE;
transmitString_F(PSTR("Total Memory: "));

displayMemory (HIGH, totalMemory);

freeClusters = getSetFreeCluster (TOTAL_FREE, GET, 0);
//freeClusters = 0xffffffff;    

if(freeClusters > _totalClusters)
{
   _freeClusterCountUpdated = 0;
   freeClusters = 0;
   totalClusterCount = 0;
   cluster = _rootCluster;    
    while(1)
    {
      sector = _unusedSectors + _reservedSectorCount + ((cluster * 4) / _bytesPerSector) ;
      SD_readSingleBlock(sector);
      for(i=0; i<128; i++)
      {
           value = (unsigned long *) &buffer[i*4];
         if(((*value)& 0x0fffffff) == 0)
            freeClusters++;;
        
         totalClusterCount++;
         if(totalClusterCount == (_totalClusters+2)) break;
      }  
      if(i < 128) break;
      cluster+=128;
    } 
}

if(!_freeClusterCountUpdated)
  getSetFreeCluster (TOTAL_FREE, SET, freeClusters); //update FSinfo next free cluster entry
_freeClusterCountUpdated = 1;  //set flag
freeMemory = freeClusters * _sectorPerCluster / 1024;
freeMemory *= _bytesPerSector ;
TX_NEWLINE;
transmitString_F(PSTR(" Free Memory: "));
displayMemory (HIGH, freeMemory);
TX_NEWLINE; 
}
*/

//********************************************************************
//Function: to delete a specified file from the root directory
//Arguments: pointer to the file name
//return: none
//********************************************************************
void deleteFile (unsigned char *fileName)
{
  unsigned char error;

  error = convertFileName (fileName);
  if(error) return;

  findFiles (DELETE, fileName);
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

void makeshortfilename(unsigned char *longfilename, unsigned char *shortfilename)
{
    // make a short file name from the given long file name
    int i,j;
    unsigned char thechar;
    j = 0;
    for (i = 0; i < 6; i++)
    {
        thechar = longfilename[i];
        
        if (longfilename[i] >= 'a' && longfilename[i] <= 'z')
        {
            thechar = longfilename[i] - 32;
        }
        
        if (thechar < 'A' || thechar > 'Z')
        {
            thechar = '_';
        }
        
        shortfilename[i] = thechar;
    }
        
    shortfilename[6] = '~';
    shortfilename[7] = '1';
    shortfilename[8] = 'P';
    shortfilename[9] = 'R';
    shortfilename[10] = 'G';
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


