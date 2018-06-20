#include<stdio.h>
#include<stdlib.h>
#include<timer.h>

//templates
void simulate(int size,int length,int sets);
//void direct(int blocks1,int blockLength);
void associative(int groups,int lines,int blockLength);
int* splitAddress(int address,int group,int blockLength);

//sample Address sequence
int addresses[]={0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64068,72,76,80,0,4,8,12,16,71,3,41,81,39,38,71,15,39,11,51,57,41};

int addresslength=39;

int main()
{
 //sample test run one:
 //128-byte,direct-mapped cache with 8-byte cache lines
 double start,finish;
 
 GET_TIME(start);
 printf("test run one\n");
 simulate(128,8,16);
 GET_TIME(finish);
 printf("Time elapsed:%f\n",finish-start);
 
 //sample test run two:
 //64-byte, 2-way set associative cache with 8-byte cache lines
 GET_TIME(start);
 printf("test run two\n");
 simulate(64,8,2);
 GET_TIME(finish);
 printf("Time elapsed:%f\n",finish-start); 
//sample test run three:
 //128-byte, direct-mapped cache with 16-byte cache lines
 GET_TIME(start); 
printf("\ntest run three\n");
 simulate(128,16,8);
GET_TIME(finish);
 printf("Time elapsed:%f\n",finish-start);
//sample test run four:
 //64-byte, fully associative cache with 8-byte cache lines
  GET_TIME(start);
printf("\ntest run four\n");
 simulate(64,8,1);
GET_TIME(finish);
printf("Time elapsed:%f\n",finish-start);
 
}

void simulate(int size,int length,int groups)
{
  int blocks = size/length;
  associative(groups,blocks,length);
}

void associative(int groups,int blocks,int blockLength)
{
 int counter = 1; 
 int subBlocks=blocks/groups;
 int numParts=4;
 int cache[groups][subBlocks][numParts];//cache=malloc(groups*subBlocks*numParts);
 int x,y,z;
 for(x=0;x<groups;x++)
 {
  for(y=0;y<subBlocks;y++)
   {
    for(z=0;z<numParts;z++)
     {
 	cache[x][y][z]=0;
     }
   }
 }
 printf("size after zeros: %d",cache);
 int i,j;
 int oldest=-1;
 int spot;
 int* cacheBlock;
 int* parts;
 #pragma omp parallel for
  for(i=0;i<addresslength;i++)
  {
  parts=splitAddress(addresses[i], groups, blockLength);
  int tag=*(parts+0);
  int group=*(parts+1);
  int hit=0;
  for(j=0;j<subBlocks;j++)
  {
   if(cache[group][j][2]&&cache[group][j][1]==tag)
   {
    hit=1;
   }
  }
  if(hit==1)
  {
   printf("%d:%d::%d-HIT\n",addresses[i],tag,group);
   hit=0;
  }
  else
  {
   for(j=0;j<subBlocks;j++)
   {
    if(oldest==-1||cache[group][j][3]<oldest)
     {
       oldest=cache[group][j][3];
       spot=j;
     }
    if(cache[group][j][2]==0)
    {
       spot=j;
       break;
    }
   }

 cache[group][spot][1]=tag;
 cache[group][spot][2]=1;
 cache[group][spot][3]=counter++;
 printf("by thread %d->%d:%d:%d-MISS\n",omp_get_thread_num(),addresses[i],tag,group);
 }
 }
 }

int* splitAddress(int address,int groups,int blockLength)
{
 int tagField=address/groups;
 int groupField=address%groups;
 int r[2]={tagField,groupField};
 int* result=r;
 return result;
}

Timer.h

#ifndef _TIMER_H_
#define _TIMER_H_

#include <sys/time.h>

/* The argument now should be a double (not a pointer to a double) */
#define GET_TIME(now) { \
   struct timeval t; \
   gettimeofday(&t, NULL); \
   now = t.tv_sec + t.tv_usec/1000000.0; \
}#endif	
