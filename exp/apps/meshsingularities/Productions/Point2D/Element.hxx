#ifndef __ELEMENT_2D_H_INCLUDED__
#define __ELEMENT_2D_H_INCLUDED__

#include "EPosition.hxx"
#include "NPosition.hxx"
#include "DoubleArgFunction.hxx"
#include "GaussianQuadrature.hxx"
#include <string.h>
#include <cstdlib>
#include <cmath>
#include <map>

namespace D2{

class DoubleArgFunctionProduct : public IDoubleArgFunction
{

	private:
		IDoubleArgFunction* function1;
		IDoubleArgFunction* function2;

	public:
		void SetFunctions(IDoubleArgFunction* _function1, IDoubleArgFunction* _function2)
		{
			function1 = _function1;
			function2 = _function2;
		}

		virtual double ComputeValue(double x, double y)
		{
			return function1->ComputeValue(x,y)*function2->ComputeValue(x,y);
		}

};

class Element{
	
	private:
		double xl; 
		double yl; 
		double xr;
		double yr;
		double* coordinates;
		bool* neighbours;
		EPosition position;
		bool is_first_tier;
		int bot_left_vertex_nr; 
		int left_edge_nr;
		int top_left_vertex_nr; 
		int top_edge_nr; 
		int top_right_vertex_nr; 
		int bot_edge_nr;
		int interior_nr;
		int right_edge_nr;
		int bot_right_vertex_nr;
		
		IDoubleArgFunction* vertex_bot_left_function;
		IDoubleArgFunction* vertex_top_left_function;
		IDoubleArgFunction* vertex_top_right_function;
		IDoubleArgFunction* vertex_bot_right_function; 
		
		IDoubleArgFunction* edge_left_function;
		IDoubleArgFunction* edge_top_function; 
		IDoubleArgFunction* edge_bot_function; 
		IDoubleArgFunction* edge_right_function; 
		
		IDoubleArgFunction* interior_function;
		
		IDoubleArgFunction** shapeFunctions;
		 
		DoubleArgFunctionProduct* product;
	public:
		Element(double* _coordinates, bool* _neighbours, EPosition position, bool is_first_tier)
			: position(position), is_first_tier(is_first_tier)
		{
			coordinates = new double[4];
			coordinates[0] = _coordinates[0]; coordinates[1] = _coordinates[1]; coordinates[2] = _coordinates[2]; coordinates[3] = _coordinates[3];
			neighbours = new bool[4];
			neighbours[0] = _neighbours[0]; neighbours[1] = _neighbours[1]; neighbours[2] = _neighbours[2]; neighbours[3] = _neighbours[3];
			xl = coordinates[0]; xr = coordinates[1]; yl = coordinates[2]; yr = coordinates[3];

			
			vertex_bot_left_function = new VertexBotLeftShapeFunction(is_first_tier, coordinates, neighbours, position);
			vertex_top_left_function = new VertexTopLeftShapeFunction(is_first_tier, coordinates, neighbours, position);
			vertex_top_right_function = new VertexTopRightShapeFunction(is_first_tier, coordinates, neighbours, position);
			vertex_bot_right_function = new VertexBotRightShapeFunction(is_first_tier, coordinates, neighbours, position);
			
			edge_left_function = new EdgeLeftShapeFunction(is_first_tier, coordinates, neighbours, position);
			edge_top_function = new EdgeTopShapeFunction(is_first_tier, coordinates, neighbours, position);
			edge_bot_function = new EdgeBotShapeFunction(is_first_tier, coordinates, neighbours, position);
			edge_right_function = new EdgeRightShapeFunction(is_first_tier, coordinates, neighbours, position);
			
			interior_function = new InteriorShapeFunction(is_first_tier, coordinates, neighbours, position);
			
			
			shapeFunctions = new IDoubleArgFunction*[9];
			shapeFunctions[0] = vertex_bot_left_function;
			shapeFunctions[1] = edge_left_function;
			shapeFunctions[2] = vertex_top_left_function; 
			shapeFunctions[3] = edge_top_function; 
			shapeFunctions[4] = vertex_top_right_function;
			shapeFunctions[5] = edge_bot_function; 
			shapeFunctions[6] = interior_function; 
			shapeFunctions[7] = edge_right_function; 
			shapeFunctions[8] = vertex_bot_right_function;

			product = new DoubleArgFunctionProduct();
					
		}
		
		~Element()
		{
			delete vertex_bot_left_function;
			delete vertex_top_left_function;
			delete vertex_top_right_function;
			delete vertex_bot_right_function;
			
			delete edge_left_function;
			delete edge_top_function; 
			delete edge_bot_function;
			delete edge_right_function; 
			
			delete interior_function; 
			delete product;
			delete[] shapeFunctions;
			delete coordinates;
			delete neighbours;
			
		}
		
		Element** CreateAnotherTier(int nr);
		Element** CreateFirstTier(int nr);
		Element** CreateLastTier(int nr); 
		

		void fillMatrix(double** tier_matrix, double** global_matrix, int start_nr_adj);
		void fillRhs(double* tier_rhs, double* global_rhs, IDoubleArgFunction* f, int start_nr_adj);
		void fillMatrices(double** tier_matrix, double** global_matrix, double* tier_rhs, double* global_rhs, IDoubleArgFunction* f, int start_nr_adj);
		bool checkSolution(std::map<int,double> *solution_map, IDoubleArgFunction* f);
		
		void set_bot_left_vertex_nr(int nr){
			bot_left_vertex_nr = nr; 
		}
		int get_bot_left_vertex_nr(){
			return bot_left_vertex_nr;
		}
		void set_top_left_vertex_nr(int nr){
			top_left_vertex_nr = nr; 
		}
		void set_top_right_vertex_nr(int nr){
			top_right_vertex_nr = nr; 
		}
		void set_bot_right_vertex_nr(int nr){
			bot_right_vertex_nr = nr; 
		}
		void set_left_edge_nr(int nr){
			left_edge_nr = nr; 
		}
		void set_top_edge_nr(int nr){
			top_edge_nr = nr; 
		}
		void set_bot_edge_nr(int nr){
			bot_edge_nr = nr; 
		}
		void set_right_edge_nr(int nr){
			right_edge_nr = nr; 
		}
		void set_interior_nr(int nr){
			interior_nr = nr; 
		}
		private:
			void comp(int indx1, int indx2, IDoubleArgFunction* f1, IDoubleArgFunction* f2,double** tier_matrix, double** global_matrix, int start_nr_adj);
};
}
#endif
