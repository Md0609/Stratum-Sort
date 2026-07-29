// Falsacion del "beneficio" de t>=96: el adversario se construye contra un
// target dado. Si la ganancia solo aparece cuando el adversario se construyo
// contra 64 y desaparece al reconstruirlo contra el t de la configuracion,
// entonces no es una propiedad del algoritmo sino sobreajuste al generador.
#include "DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
using drs::DynamicRangeSort; using drs::testing::DataVector; using drs::testing::DatasetGenerator;
int main(){
  const std::size_t n=1000000, reps=7;
  const std::vector<std::pair<std::size_t,std::size_t>> cfg={{32,64},{24,96},{32,96},{32,128},{16,96}};
  std::cout<<std::left<<std::setw(26)<<"adversario construido con"<<std::right;
  for(auto&c:cfg) std::cout<<std::setw(13)<<("("+std::to_string(c.first)+","+std::to_string(c.second)+")");
  std::cout<<"\n"<<std::string(26+13*cfg.size(),'-')<<"\n";
  for(std::size_t core : {64u,96u,128u,160u}){
    DatasetGenerator g; const DataVector d=g.adversarialPeeling(n,core);
    std::cout<<std::left<<std::setw(26)<<("core="+std::to_string(core))<<std::right;
    for(auto&k:cfg){std::vector<double> s;
      for(std::size_t r=0;r<reps;++r){DataVector cp=d;DynamicRangeSort<int64_t> so(k.first,k.second);
        auto t0=std::chrono::steady_clock::now();so.sort(cp);auto t1=std::chrono::steady_clock::now();
        s.push_back(std::chrono::duration<double,std::milli>(t1-t0).count());}
      std::sort(s.begin(),s.end());
      std::cout<<std::setw(13)<<std::fixed<<std::setprecision(1)<<s[reps/2];}
    std::cout<<"\n";}
  std::cout<<"\n(ms; la columna (32,64) es la configuracion actual)\n";}
