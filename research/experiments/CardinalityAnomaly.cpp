#include "stratum/StratumSort.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
using stratum::StratumSort;
int main(){
  const std::size_t n=1000000;
  std::cout<<std::left<<std::setw(16)<<"valores distintos"<<std::right<<std::setw(10)<<"ms"
           <<std::setw(9)<<"bins"<<std::setw(12)<<"comparac."<<std::setw(10)<<"subdiv"<<"\n";
  for(int k : {1,2,3,5,17,33,65,1000}){
    std::vector<int64_t> v(n);
    for(std::size_t i=0;i<n;++i) v[i]=(int64_t)(i%k);
    std::vector<double> s; std::size_t bins=0,cmp=0,sub=0;
    for(int r=0;r<15;++r){auto c=v;StratumSort<int64_t> so;
      auto t0=std::chrono::steady_clock::now();so.sort(c);auto t1=std::chrono::steady_clock::now();
      s.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
      if(r==0){bins=so.metrics().totalBins();cmp=so.metrics().comparisons();sub=so.metrics().subdivisions();}
      if(!std::is_sorted(c.begin(),c.end())){std::cout<<"INCORRECTO\n";return 1;}}
    std::sort(s.begin(),s.end());
    std::cout<<std::left<<std::setw(16)<<k<<std::right<<std::setw(10)<<std::fixed<<std::setprecision(2)
             <<s[7]<<std::setw(9)<<bins<<std::setw(12)<<cmp<<std::setw(10)<<sub<<"\n";}
}
