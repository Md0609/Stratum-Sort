#include "drs/DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
using drs::DynamicRangeSort; using drs::testing::DataVector; using drs::testing::DatasetGenerator;
int main(){
  const std::size_t n=1000000, reps=5;
  const std::vector<std::pair<std::size_t,std::size_t>> cfg={
    {16,16},{16,32},{16,48},{16,64},{16,96},
    {24,24},{24,48},{24,64},{24,96},
    {32,32},{32,48},{32,64},{32,96},{32,128},
    {48,48},{48,64},{48,96},{48,128},
    {64,64},{64,96},{64,128}};
  struct C{const char*n;DataVector(*f)(DatasetGenerator&,std::size_t);};
  C cs[]={{"RandomUniform",[](DatasetGenerator&g,std::size_t m){return g.randomUniform(m);}},
          {"SortedAscending",[](DatasetGenerator&g,std::size_t m){return g.sortedAscending(m);}},
          {"SortedDescending",[](DatasetGenerator&g,std::size_t m){return g.sortedDescending(m);}},
          {"ManyRepeated",[](DatasetGenerator&g,std::size_t m){return g.manyRepeated(m);}},
          {"NormalGaussian",[](DatasetGenerator&g,std::size_t m){return g.normalDistribution(m);}},
          {"Concentrated",[](DatasetGenerator&g,std::size_t m){return g.concentrated(m);}},
          {"SmallRangeManyEl",[](DatasetGenerator&g,std::size_t m){return g.smallRangeManyElements(m);}},
          {"HugeRangeFewEl",[](DatasetGenerator&g,std::size_t m){return g.hugeRangeFewElements(m);}},
          {"FullRangeExtremes",[](DatasetGenerator&g,std::size_t m){return g.fullRangeExtremes(m);}},
          {"AdversarialPeeling",[](DatasetGenerator&g,std::size_t m){return g.adversarialPeeling(m,64);}}};
  for(auto&c:cs){
    DatasetGenerator g; const DataVector d=c.f(g,n);
    std::vector<std::vector<double>> s(cfg.size());
    for(std::size_t r=0;r<reps;++r)
      for(std::size_t i=0;i<cfg.size();++i){
        DataVector cp=d; DynamicRangeSort<int64_t> so(cfg[i].first,cfg[i].second);
        auto t0=std::chrono::steady_clock::now(); so.sort(cp);
        auto t1=std::chrono::steady_clock::now();
        if(!std::is_sorted(cp.begin(),cp.end())){std::cout<<"INCORRECTO\n";return 1;}
        s[i].push_back(std::chrono::duration<double,std::milli>(t1-t0).count());
      }
    for(std::size_t i=0;i<cfg.size();++i){
      std::sort(s[i].begin(),s[i].end());
      std::cout<<c.n<<" "<<cfg[i].first<<" "<<cfg[i].second<<" "
               <<std::fixed<<std::setprecision(3)<<s[i][reps/2]<<"\n";}
    std::cout.flush();}
}
