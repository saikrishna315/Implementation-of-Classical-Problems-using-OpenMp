#include<stdio.h>
#include<stdlib.h>
#include<omp.h>
int board[4][4]={0};
int mat[4][4][4]={0};
int sflag=0;
int nsol[4]={1,1,1,1},newsol;
int comp[4]={0};
/* Initializing the soltion vector Matrix with the first queen placement*/
void initialize()
{
  int i;
  for(i=0;i<4;i++)
   mat[i][0][0]=i+1;
}
/* prints the solution Vector*/
void printmat(int b[4][4][4],int rank)
{
  int i,j;
   printf("\n Final Solution by rank %d\n",(rank+3)%4);
  for(i=0;i<4;i++)
  {
   if(b[rank][i][0]==0)
     continue; 
   for(j=0;j<4;j++)
    printf("%2d",b[rank][i][j]);
  }	 
}
/* reinitializing the queen solver matrix for next iteration */
void reinit()
{
  int i,j;
  for(i=0;i<4;i++)
   for(j=0;j<4;j++)
    board[i][j]=0;
}
/* reinitializing a vector from solution matrix for invalid solution*/
void reinitv(int row,int rank)
{
 int i;
 for(i=0;i<4;i++)
  mat[rank][row][i]=0;
}
/* checking safe positions for placement of queen in a particular row and column */
int safe(int b[4][4],int r,int c)
{
	int i,j;
	/* checking for the upper elements in the column */
	for(i=0;i<r;i++)
	 if(b[i][c])
	  return 0;
	/* checking for the left upper diagonal */
	for(i=r,j=c; (i>=0 && j>=0) ; i--,j--)
	 if(b[i][j])
	  return 0;
	/* checking for the right upper diagonal */
	for(i=r,j=c; (i>=0 && j<4) ; i--,j++)
	 if(b[i][j])
	  return 0;
	/* If false is not returned the row and column provided to the function is valid */
	return 1;
}
/* If the solution Multiplies copy the vector and continue placing */
void copy(int sol,int row,int tr,int rank)
{
  int i;
  if( sflag!=0)
  {
      for(i=0;i<sol;i++)
   {
    mat[rank][row][i]=mat[rank][tr][i];
   } 
  } 
}
/* The main Function */
void main()
{
  int i,j,ctr=0,c,init,rank;
  initialize();
  #pragma omp parallel num_threads(4) private(newsol,rank,i,j,ctr,init,c)
  {
    rank=omp_get_thread_num();
  for(c=1;c<4;c++)
  { 
      rank=(rank+3)%4;
      printf("\n ROW %d : Thread %d Solving Thread %d's Solutions",c+1,omp_get_thread_num(),rank);
       newsol=nsol[rank];
      for(i=0;i<nsol[rank];i++)
      {
       init=i; 
       if(mat[rank][i][0]==0)  // No new Solutions available
       {
         continue;
       }  
       ctr=0;
       sflag=0;
       reinit(board);
       for(j=0;j<c;j++) 
       {
        if(mat[rank][i][j]!=0) 
         board[j][mat[rank][i][j]-1]=1; 
        else
         ctr++;		
       }
       if(ctr!=(4-c)) 
       {
        for(j=0;j<4;j++) 	
         if(safe(board,c,j))
         {
          comp[omp_get_thread_num()]++;	
          if(sflag==1)			
          { copy(c,newsol,init,rank); 
            mat[rank][newsol][c]=j+1;  
            newsol++; 
	  }
          else      
	   mat[rank][i][c]=j+1;    
 	  sflag=1;
          }
       } 
       if(sflag==0)
       {
        reinitv(i,rank);
       }
      }// end of 'i' for loop
      nsol[rank]=newsol; 
      #pragma omp barrier
  } // end of 'c' for loop  
    #pragma omp critical
    printmat(mat,rank); 
    printf("\n");
  } // the parallel block 
   for(i=0;i<4;i++)
   printf("\n Computations performed By thread %d :  %d",i,comp[i]);
}







