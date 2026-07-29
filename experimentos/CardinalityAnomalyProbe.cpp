// Falsacion de la hipotesis de conflicto de cache en la anomalia de baja
// cardinalidad. Con k=2 la dispersion escribe en DOS flujos separados por
// (fraccion de ceros)*n*8 bytes. Si la causa es aliasing de conjuntos de
// cache, el coste debe depender de esa separacion, no de k.
#include "DynamicRangeSort.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
using drs::DynamicRangeSort;
int main(){
  const std::size_t n=1000000;
  std::cout<<"k=2, variando la fraccion de ceros => variando la separacion entre flujos\n\n";
  std::cout<<std::left<<std::setw(10)<<"frac 0"<<std::right<<std::setw(14)<<"separacion"
           <<std::setw(10)<<"ms"<<"\n"<<std::string(34,'-')<<"\n";
  for (int pct : {5,10,20,25,30,40,50,60,70,75,80,90,95}) {
    std::vector<int64_t> v(n);
    const std::size_t zeros = n*pct/100;
    for(std::size_t i=0;i<n;++i) v[i] = (i%100) < (std::size_t)pct ? 0 : 1;
    std::vector<double> s;
    for(int r=0;r<11;++r){auto c=v;DynamicRangeSort<int64_t> so;
      auto t0=std::chrono::steady_clock::now();so.sort(c);auto t1=std::chrono::steady_clock::now();
      s.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());}
    std::sort(s.begin(),s.end());
    std::cout<<std::left<<std::setw(10)<<pct<<std::right<<std::setw(11)
             <<std::fixed<<std::setprecision(2)<<(zeros*8.0/1048576)<<" MB"
             <<std::setw(10)<<std::setprecision(2)<<s[5]<<"\n";
  }
  std::cout<<"\nControl: mismo n, k flujos uniformes\n";
  std::cout<<std::left<<std::setw(10)<<"k"<<std::right<<std::setw(14)<<"separacion"<<std::setw(10)<<"ms"<<"\n";
  for (int k : {1,2,4,8,16}) {
    std::vector<int64_t> v(n);
    for(std::size_t i=0;i<n;++i) v[i]=(int64_t)(i%k);
    std::vector<double> s;
    for(int r=0;r<11;++r){auto c=v;DynamicRangeSort<int64_t> so;
      auto t0=std::chrono::steady_clock::now();so.sort(c);auto t1=std::chrono::steady_clock::now();
      s.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());}
    std::sort(s.begin(),s.end());
    std::cout<<std::left<<std::setw(10)<<k<<std::right<<std::setw(11)
             <<std::fixed<<std::setprecision(2)<<(n/k*8.0/1048576)<<" MB"<<std::setw(10)<<s[5]<<"\n";
  }
}
