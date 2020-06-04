/*
  Copyright 2013, 2014, 2015 SINTEF ICT, Applied Mathematics.
  Copyright 2014 Dr. Blatt - HPC-Simulation-Software & Services
  Copyright 2015, 2017 IRIS AS

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <opm/simulators/flow/Main.hpp>

#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <fstream>


int main(int argc, char** argv)
{

    std::ofstream myfile2;
    myfile2.open("/home/ubuntu/OPM_STARTING_TIME.txt");
    
    double t1, t2, elapsed;
    struct timeval tp;
    struct timezone tzp;
    gettimeofday(&tp, &tzp);
    t1 = ((double) tp.tv_sec+ (double) tp.tv_usec*1.e-6);

    myfile2 << std::fixed << t1;
    myfile2.close();


    auto mainObject = Opm::Main(argc, argv);
    int res = mainObject.runDynamic();


    struct timeval tp2;
    struct timezone tzp2;
    gettimeofday(&tp2, &tzp2);
    t2 = ((double) tp2.tv_sec+ (double) tp2.tv_usec*1.e-6);
    elapsed = t2-t1;
    printf("\n");
    printf("[MO833] Total time,%f\n", elapsed);



    std::string line3;
    std::ifstream myfile3 ("/home/ubuntu/OPM_TOTAL_PARAMOUNT_TIME.txt");
    getline (myfile3,line3);
    myfile3.close();


    const char *line4 = line3.c_str();
    double total_pi_time = atof(line4);

    printf("\n");
    if(total_pi_time>0){
      double beta = (elapsed-total_pi_time)/total_pi_time;
      printf("[MO833] Beta: %f\n", beta);
    }else{
      printf("[MO833] Beta: 1\n");
    }

    return res;
}

