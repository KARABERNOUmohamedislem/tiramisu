#include "Halide.h"
#include "function_jacobi1d_MINI_wrapper.h"
#include "tiramisu/utils.h"
#include <iostream>
#include <time.h>
#include <fstream>
#include <chrono>

using namespace std::chrono;
using namespace std;      

int main(int, char **argv)
{
    double *c_b_A = (double*)malloc(30* sizeof(double));
	parallel_init_buffer(c_b_A, 30, (double)61);
	Halide::Buffer<double> b_A(c_b_A, 30);

    double *c_b_B = (double*)malloc(30* sizeof(double));
	parallel_init_buffer(c_b_B, 30, (double)61);
	Halide::Buffer<double> b_B(c_b_B, 30);

	int nb_exec = get_nb_exec();

	for (int i = 0; i < nb_exec; i++) 
	{  
		auto begin = std::chrono::high_resolution_clock::now(); 
		function_jacobi1d_MINI(b_A.raw_buffer(),b_B.raw_buffer());
		auto end = std::chrono::high_resolution_clock::now(); 

		std::cout << std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count() / (double)1000000 << " " << std::flush; 
	}
		std::cout << std::endl;

	return 0; 
}