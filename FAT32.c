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
#include "IEEE488.h"

//***************************************************************************
//Function: to read data from boot sector of SD card, to determine important
//parameters like bytesPerSector, sectorsPerCluster etc.
//Arguments: none
//return: none
//***************************************************************************
unsigned char getBootSectorData (void)
{
struct BS_Structure *bpb; //mapping the buffer onto the structure
struct MBRinfo_Structure *mbr;
struct partitionInfo_Structure *partition;
unsigned long dataSectors;

unusedSectors = 0;

SD_readSingleBlock(0);
bpb = (struct BS_Structure *)buffer;

if(bpb->jumpBoot[0]!=0xE9 && bpb->jumpBoot[0]!=0xEB)   //check if it is boot sector
{
  mbr = (struct MBRinfo_Structure *) buffer;       //if it is not boot sector, it must be MBR
  
  if(mbr->signature != 0xaa55) return 1;       //if it is not even MBR then it's not FAT32
  	
  partition = (struct partitionInfo_Structure *)(mbr->partitionData);//first partition
  unusedSectors = partition->firstSector; //the unused sectors, hidden to the FAT
  
  SD_readSingleBlock(partition->firstSector);//read the bpb sector
  bpb = (struct BS_Structure *)buffer;
  if(bpb->jumpBoot[0]!=0xE9 && bpb->jumpBoot[0]!=0xEB) return 1; 
}

bytesPerSector = bpb->bytesPerSector;
//transmitHex(INT, bytesPerSector); transmitByte(' ');
sectorPerCluster = bpb->sectorPerCluster;
//transmitHex(INT, sectorPerCluster); transmitByte(' ');
reservedSectorCount = bpb->reservedSectorCount;
rootCluster = bpb->rootCluster;// + (sector / sectorPerCluster) +1;
//transmitString("rootCluster: ");
transmitHex(LONG, rootCluster);
firstDataSector = bpb->hiddenSectors + reservedSectorCount + (bpb->numberofFATs * bpb->FATsize_F32);

dataSectors = bpb->totalSectors_F32
              - bpb->reservedSectorCount
              - ( bpb->numberofFATs * bpb->FATsize_F32);
totalClusters = dataSectors / sectorPerCluster;
//transmitHex(LONG, totalClusters); transmitByte(' ');

if((getSetFreeCluster (TOTAL_FREE, GET, 0)) > totalClusters)  //check if FSinfo free clusters count is valid
     freeClusterCountUpdated = 0;
else
	 freeClusterCountUpdated = 1;
return 0;
}

//***************************************************************************
//Function: to calculate first sector address of any given cluster
//Arguments: cluster number for which first sector is to be found
//return: first sector address
//***************************************************************************
unsigned long getFirstSector(unsigned long clusterNumber)
{
  return (((clusterNumber - 2) * sectorPerCluster) + firstDataSector);
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
FATEntrySector = unusedSectors + reservedSectorCount + ((clusterNumber * 4) / bytesPerSector) ;

//get the offset address in that sector number
FATEntryOffset = (unsigned int) ((clusterNumber * 4) % bytesPerSector);

//read the sector into a buffer
while(retry <10)
{ if(!SD_readSingleBlock(FATEntrySector)) break; retry++;}

//get the cluster address from the buffer
FATEntryValue = (unsigned long *) &buffer[FATEntryOffset];

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
struct FSInfo_Structure *FS = (struct FSInfo_Structure *) &buffer;
unsigned char error;

SD_readSingleBlock(unusedSectors + 1);

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
 
   error = SD_writeSingleBlock(unusedSectors + 1);	//update FSinfo
 }
 return 0xffffffff;
}

//***************************************************************************
//Function: to get DIR/FILE list or a single file address (cluster number) or to delete a specified file
//Arguments: #1 - flag: GET_LIST, GET_FILE or DELETE #2 - pointer to file name (0 if arg#1 is GET_LIST)
//return: first cluster of the file, if flag = GET_FILE
//        print file/dir list of the root directory, if flag = GET_LIST
//		  Delete the file mentioned in arg#2, if flag = DELETE
//****************************************************************************
struct dir_Structure* ListFilesIEEE ()
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
cluster = rootCluster; //root cluster
is_long_entry = 0;
dir_start = 0x041f;
file = 0;
while(1)
{
   firstSector = getFirstSector (cluster);

   for(sector = 0; sector < sectorPerCluster; sector++)
   {
     SD_readSingleBlock (firstSector + sector);

     for(i=0; i<bytesPerSector; i+=32)
     {
	    dir = (struct dir_Structure *) &buffer[i];

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
            longent = (struct dir_Longentry_Structure *) &buffer[i];
            
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

cluster = rootCluster; //root cluster
is_long_entry = 0;
for (i = 0; i < 32; i++)
{
    lentstr[i] = 0;
}
is_long_entry_match = 1;
if (cmp_long_fname == 1)
{
    fname_len = strlen(fileName);
    wildcard = 0;
    for (i = 0; i < fname_len; i++)
    {
        if (fileName[i] == '*')
        {
            wildcard = 1;
        }
    }
}
else 
{
    // if a short name, check for a wildcard character
    wildcard = 0;
    for (i = 0; i < 8; i++)
    {
        if (fileName[i] == '*')
        {
            wildcard = 1;
        }
    }
}


while(1)
{
   firstSector = getFirstSector (cluster);

   for(sector = 0; sector < sectorPerCluster; sector++)
   {
     SD_readSingleBlock (firstSector + sector);
	

     for(i=0; i<bytesPerSector; i+=32)
     {
	    dir = (struct dir_Structure *) &buffer[i];

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
                
            if (wildcard == 1)
            {
                if ((dir->name[8] == 'P' && dir->name[9] == 'R' && dir->name[10] == 'G') ||
                    (dir->name[8] == 'B' && dir->name[9] == 'I' && dir->name[10] == 'N'))
                {
                    //transmitString(dir->name);
                    // is the first character a wildcard?
                    if (fileName[0] == '*')
                    {
                        j = 11;
                    }
                    else 
                    {
                        // check against this filename
                        if (is_long_entry == 1)
                        {
                        
                            if (cmp_long_fname == 1)
                            {
                            
                                //transmitString("LvL:");
                                //transmitString(lentstr);
                                //transmitString(fileName);
                                
                                // compare long name against long name
                                for (j = 0; j < fname_len; j++)
                                {
                                    temp = lentstr[j];
                                    temp2 = fileName[j];
                                    
                                    // set characters to uppercase
                                    if (temp >= 'a' && temp <= 'z')
                                    {
                                        temp -= 32;
                                    }
                                    
                                    if (temp2 >= 'a' && temp2 <= 'z')
                                    {
                                        temp2 -= 32;
                                    }
                                    
                                    //transmitByte(temp);
                                    //transmitByte(temp2);
                                    
                                    if (temp != temp2)
                                    {
                                        // check for wildcard
                                        if (fileName[j] == '*')
                                        {
                                            // what's after doesn't matter, this is a match
                                            j = 11;
                                            break;
                                        }
                                        else
                                        {
                                            // this is not a match
                                            j = 0;
                                            break;
                                        }
                                    }
                                    
                                }
                            }
                            else
                            {
                                // compare short name against long name
                                for (j = 0; j < 11; j++)
                                {
                                    temp = lentstr[j];
                                    if (temp >= 'a' && temp <= 'z')
                                    {
                                        temp -= 32;
                                    }
                                    
                                    if (temp != fileName[j])
                                    {
                                        if (fileName[j] == '*')
                                        {  
                                            j = 11;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        else 
                        {
                            // checking short name against short name
                            //transmitString("@");
                            //transmitString(fileName);
                            
                            /*
                            for (j = 0; j < 8; j++)
                            {
                                transmitByte(dir->name[i]);
                            }
                            */
                            
                            for (j = 0; j < 8; j++)
                            {                                
                                if (dir->name[j] != fileName[j])
                                {
                                    // is this a wildcard?
                                    if (fileName[j] == '*')
                                    {
                                        j = 11;
                                    }
                                    break;                             
                                }
                            }
                            
                            
                        }

                    }

                }
            }
            else 
            {
                if (cmp_long_fname == 1)
                {
                    //transmitString("in cmplongfname");
                    
                    // check long file name entry for exact match
                    j = 0;
                    
                    this_fname_len = index;
                    if (is_long_entry == 1)
                    {
                        // do long entry check
                        //transmitString(lentstr);
                        //transmitString(fileName);
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
            }

            //transmitString("here I am: ");
            //transmitHex(INT, j);
            
                
            if(j == 11)
			{
              // found the right file, proceed.
              
			  if(flag == GET_FILE)
              {
			    appendFileSector = firstSector + sector;
				appendFileLocation = i;
				appendStartCluster = (((unsigned long) dir->firstClusterHI) << 16) | dir->firstClusterLO;
				fileSize = dir->fileSize;
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
                
                longent = (struct dir_Longentry_Structure *) &buffer[i];
                
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
            //transmitString(lentstr);
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
unsigned long cluster, byteCounter = 0, fileSize, firstSector;
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

    // for testing only
    // testing only
    /*
    fileName[filename_position++] = 'B';
    fileName[filename_position++] = '.';
    fileName[filename_position++] = 'P';
    fileName[filename_position++] = 'R';
    fileName[filename_position++] = 'G';
    fileName[filename_position] = 0;
    */

    j = readFile (VERIFY, fileName);

    if(j == 1) 
    {
        //transmitString("exists, deleting.");
        findFiles (DELETE, fileName);
        j = 0;
    }
    
    /*
    if(j == 2)
    { 
       return; //invalid file name
    }
    else
    {
    */
      //TX_NEWLINE;
      //transmitString_F(PSTR(" Creating File.."));

      cluster = getSetFreeCluster (NEXT_FREE, GET, 0);
      if(cluster > totalClusters)
         cluster = rootCluster;

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
      fileSize = 0;
    //}

    //transmitString_F(PSTR("first cluster is: "));
    //transmitHex(LONG, cluster);
    //TX_NEWLINE;
    
    fileStartCluster = cluster;
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
  cluster = appendStartCluster;
  clusterCount=0;
  while(1)
  {
    nextCluster = getSetNextCluster (cluster, GET, 0);
    if(nextCluster == EOF) break;
	cluster = nextCluster;
	clusterCount++;
  }

  sector = (fileSize - (clusterCount * sectorPerCluster * bytesPerSector)) / bytesPerSector; //last sector number of the last cluster of the file
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
  if(cluster > totalClusters)
     cluster = rootCluster;

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
  fileSize = 0;
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
	  i = fileSize % bytesPerSector;
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
		fileSize++;
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
		 fileSize--;
	   } 
	   continue;     
	 }
	 transmitByte(data);
     buffer[i++] = data;
	 fileSize++;
     if(data == '\r')  //'Carriege Return (CR)' character
     {
        if(i == 512)
		   sectorEndFlag = 1;  //flag to indicate that the appended '\n' char should be put in the next sector
	    else
		{ 
		   transmitByte ('\n');
           buffer[i++] = '\n'; //appending 'Line Feed (LF)' character
		   fileSize++;
	    }
     }
	 
     if(i >= 512)   //though 'i' will never become greater than 512, it's kept here to avoid 
	 {				//infinite loop in case it happens to be greater than 512 due to some data corruption
	   i=0;
	   error = SD_writeSingleBlock (startBlock);
       j++;
	   if(j == sectorPerCluster) {j = 0; break;}
	   startBlock++; 
     }
	}while (data != '~');

   if(data == '~') 
   {
      fileSize--;	//to remove the last entered '~' character
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
  SD_readSingleBlock (appendFileSector);    
  dir = (struct dir_Structure *) &buffer[appendFileLocation]; 
  extraMemory = fileSize - dir->fileSize;
  dir->fileSize = fileSize;
  SD_writeSingleBlock (appendFileSector);
  freeMemoryUpdate (REMOVE, extraMemory); //updating free memory count in FSinfo sector;

  
  TX_NEWLINE;
  transmitString_F(PSTR(" File appended!"));
  TX_NEWLINE;
  return;
}

//executes following portion when new file is created

prevCluster = rootCluster; //root cluster

while(1)
{
   firstSector = getFirstSector (prevCluster);

   for(sector = 0; sector < sectorPerCluster; sector++)
   {
     SD_readSingleBlock (firstSector + sector);
	

     for(i=0; i<bytesPerSector; i+=32)
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
		  dir->fileSize = fileSize;

		  SD_writeSingleBlock (firstSector + sector);
		  fileCreatedFlag = 1;

		  TX_NEWLINE;
		  TX_NEWLINE;
		  transmitString_F(PSTR(" File Created!"));

		  freeMemoryUpdate (REMOVE, fileSize); //updating free memory count in FSinfo sector
	     
        }
     }
   }

   cluster = getSetNextCluster (prevCluster, GET, 0);

   if(cluster > 0x0ffffff6)
   {
      if(cluster == EOF)   //this situation will come when total files in root is multiple of (32*sectorPerCluster)
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
    
    /*
    for (j = fname_len+1; j < 39; j++)
    {
        fileName[j] = 0xffff;
    }
    */
    
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
	  i = fileSize % bytesPerSector;
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
	 fileSize++;
	 
     if(i >= 512)   //though 'i' will never become greater than 512, it's kept here to avoid 
	 {				//infinite loop in case it happens to be greater than 512 due to some data corruption
	   i=0;
	   error = SD_writeSingleBlock (startBlock);
       j++;
	   if(j == sectorPerCluster)
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
  SD_readSingleBlock (appendFileSector);    
  dir = (struct dir_Structure *) &buffer[appendFileLocation]; 
  extraMemory = fileSize - dir->fileSize;
  dir->fileSize = fileSize;
  SD_writeSingleBlock (appendFileSector);
  freeMemoryUpdate (REMOVE, extraMemory); //updating free memory count in FSinfo sector;

  
  TX_NEWLINE;
  transmitString_F(PSTR(" File appended!"));
  TX_NEWLINE;
  return;
}

//executes following portion when new file is created

prevCluster = rootCluster; //root cluster

while(1)
{
   firstSector = getFirstSector (prevCluster);

   for(sector = 0; sector < sectorPerCluster; sector++)
   {
     SD_readSingleBlock (firstSector + sector);
	

     for(i=0; i<bytesPerSector; i+=32)
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
                dir->fileSize = fileSize;

                SD_writeSingleBlock (firstSector + sector);
                fileCreatedFlag = 1;

                TX_NEWLINE;
                TX_NEWLINE;
                transmitString_F(PSTR(" File Created!"));

                freeMemoryUpdate (REMOVE, fileSize); //updating free memory count in FSinfo sector
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
      if(cluster == EOF)   //this situation will come when total files in root is multiple of (32*sectorPerCluster)
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
    for(cluster =startCluster; cluster <totalClusters; cluster+=128) 
    {
      sector = unusedSectors + reservedSectorCount + ((cluster * 4) / bytesPerSector);
      SD_readSingleBlock(sector);
      for(i=0; i<128; i++)
      {
       	 value = (unsigned long *) &buffer[i*4];
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


totalMemory = totalClusters * sectorPerCluster / 1024;
totalMemory *= bytesPerSector;

TX_NEWLINE;
TX_NEWLINE;
transmitString_F(PSTR("Total Memory: "));

displayMemory (HIGH, totalMemory);

freeClusters = getSetFreeCluster (TOTAL_FREE, GET, 0);
//freeClusters = 0xffffffff;    

if(freeClusters > totalClusters)
{
   freeClusterCountUpdated = 0;
   freeClusters = 0;
   totalClusterCount = 0;
   cluster = rootCluster;    
    while(1)
    {
      sector = unusedSectors + reservedSectorCount + ((cluster * 4) / bytesPerSector) ;
      SD_readSingleBlock(sector);
      for(i=0; i<128; i++)
      {
           value = (unsigned long *) &buffer[i*4];
         if(((*value)& 0x0fffffff) == 0)
            freeClusters++;;
        
         totalClusterCount++;
         if(totalClusterCount == (totalClusters+2)) break;
      }  
      if(i < 128) break;
      cluster+=128;
    } 
}

if(!freeClusterCountUpdated)
  getSetFreeCluster (TOTAL_FREE, SET, freeClusters); //update FSinfo next free cluster entry
freeClusterCountUpdated = 1;  //set flag
freeMemory = freeClusters * sectorPerCluster / 1024;
freeMemory *= bytesPerSector ;
TX_NEWLINE;
transmitString_F(PSTR(" Free Memory: "));
displayMemory (HIGH, freeMemory);
TX_NEWLINE; 
}
*/

//************************************************************
//Function: To convert the unsigned long value of memory into 
//          text string and send to UART
//Arguments: 1. unsigned char flag. If flag is HIGH, memory will be displayed in KBytes, else in Bytes. 
//			 2. unsigned long memory value
//return: none
//************************************************************

/*
void displayMemory (unsigned char flag, unsigned long memory)
{
  unsigned char memoryString[] = "              Bytes"; //19 character long string for memory display
  unsigned char i;
  for(i=12; i>0; i--) //converting freeMemory into ASCII string
  {
    if(i==5 || i==9) 
	{
	   memoryString[i-1] = ',';  
	   i--;
	}
    memoryString[i-1] = (memory % 10) | 0x30;
    memory /= 10;
	if(memory == 0) break;
  }
  if(flag == HIGH)  memoryString[13] = 'K';
  transmitString(memoryString);
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

  if(freeClusterCountUpdated)
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


