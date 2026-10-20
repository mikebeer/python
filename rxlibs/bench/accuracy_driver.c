#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "rxsp.h"
int main(int argc,char**argv){ char nm[32],op[16]; double x,a,b,l,s; 
 while(scanf("%15s %31s %lf %lf %lf %lf %lf",op,nm,&x,&a,&b,&l,&s)==7){ double v;
  if(!strcmp(op,"pdf"))v=rxsp_dist_pdf(nm,x,a,b,l,s); else if(!strcmp(op,"cdf"))v=rxsp_dist_cdf(nm,x,a,b,l,s);
  else if(!strcmp(op,"sf"))v=rxsp_dist_sf(nm,x,a,b,l,s); else if(!strcmp(op,"ppf"))v=rxsp_dist_ppf(nm,x,a,b,l,s);
  else if(!strcmp(op,"gamma"))v=rxsp_gamma(x); else if(!strcmp(op,"lgamma"))v=rxsp_lgamma(x); else if(!strcmp(op,"erf"))v=rxsp_erf(x);
  else if(!strcmp(op,"erfc"))v=rxsp_erfc(x); else if(!strcmp(op,"erfinv"))v=rxsp_erfinv(x); else if(!strcmp(op,"beta"))v=rxsp_beta(x,a);
  else if(!strcmp(op,"betainc"))v=rxsp_betainc(x,a,b); else if(!strcmp(op,"gammainc"))v=rxsp_gammainc(x,a); else if(!strcmp(op,"gammaincc"))v=rxsp_gammaincc(x,a);
  else v=0; printf("%.17g\n",v);} return 0;}
