#include "MatrixGenerator.hxx"

using namespace D2Edge;


void set_numbers(Element** elements, int row_nr, int last_row_nr, int start_nr)
{
	int first_element_in_row_nr = pow(2,row_nr+1) - 2;
	int nr_of_elements_in_row = pow(2,row_nr+1);
	if(row_nr == last_row_nr)
		nr_of_elements_in_row /= 2;

	int next_start_nr;

	for(int i = 0; i<nr_of_elements_in_row; i+=2)
	{
		Element* left_element = elements[i + first_element_in_row_nr];
		Element* right_element = elements[i + first_element_in_row_nr + 1];
		bool top_edge_constraint = (row_nr != 0 && row_nr != last_row_nr);
		left_element->set_top_left_vertex_nr(start_nr++);
		left_element->set_top_edge_nr(start_nr++);
		left_element->set_top_right_vertex_nr(start_nr);
		if(top_edge_constraint)
			start_nr-=2;

		right_element->set_top_left_vertex_nr(start_nr++);
		right_element->set_top_edge_nr(start_nr++);
		right_element->set_top_right_vertex_nr(start_nr);
		if(i+2 == nr_of_elements_in_row)
			start_nr++;
	}

	for(int i = 0; i<nr_of_elements_in_row; i+=2)
	{
		Element* left_element = elements[i + first_element_in_row_nr];
		Element* right_element = elements[i + first_element_in_row_nr + 1];
		left_element->set_left_edge_nr(start_nr++);
		left_element->set_interior_nr(start_nr++);
		left_element->set_right_edge_nr(start_nr);

		right_element->set_left_edge_nr(start_nr++);
		right_element->set_interior_nr(start_nr++);
		right_element->set_right_edge_nr(start_nr);
		if(i+2 == nr_of_elements_in_row)
			start_nr++;
	}

	next_start_nr = start_nr;

	for(int i = 0; i<nr_of_elements_in_row; i+=2)
	{
		Element* left_element = elements[i + first_element_in_row_nr];
		Element* right_element = elements[i + first_element_in_row_nr + 1];
		left_element->set_bot_left_vertex_nr(start_nr++);
		left_element->set_bot_edge_nr(start_nr++);
		left_element->set_bot_right_vertex_nr(start_nr);

		right_element->set_bot_left_vertex_nr(start_nr++);
		right_element->set_bot_edge_nr(start_nr++);
		right_element->set_bot_right_vertex_nr(start_nr);
		if(i+2 == nr_of_elements_in_row)
			start_nr++;
	}
	if(row_nr + 1 <= last_row_nr)
		set_numbers(elements,row_nr + 1,last_row_nr,next_start_nr);
}

void MatrixGenerator::CreateTiers(int to_create, int element_id, double size, double* coordinates, IDoubleArgFunction* f, bool first_tier)
{

	bool neighbours[4];
	neighbours[0] = true; neighbours[1] = true; neighbours[2] = true; neighbours[3] = true;

	if(to_create == 1)
	{

		coordinates[2] -= size;
		coordinates[3] -= size;
		Element* element;
		//position doesn't matter
		if(element_id % 2)
			element = new Element(coordinates,neighbours,BOT_RIGHT);
		else
			element = new Element(coordinates,neighbours,BOT_LEFT);

		int parent_id = element_id;
		if(element_id % 4)
			parent_id -= 2;
		parent_id /= 2;

		int row_nr = 0;
		while( (pow(2,row_nr+1) - 2) < parent_id)
		{

			row_nr++;
		}
		if( ((pow(2,row_nr+1) - 2) == parent_id) && (element_id % 4))
			row_nr++;

		if(element_id % 4)
			element_id = parent_id + pow(2,row_nr);
		else
			element_id = parent_id + pow(2,row_nr) - 1;


		elements[element_id] = element;
		tier_vector->push_back(new Tier(element,f,matrix,rhs));
		return;
	}

	double xl; double xr; double yl; double yr;
	size /= 2.0;

	if(first_tier)
	{

		coordinates[1] = coordinates[0] + size;
		coordinates[2] += size;
		coordinates[3] = coordinates[2] + size;

	}
	else
	{
		neighbours[TOP] = false;
		coordinates[1] -= size;
		coordinates[2] -= size;
		coordinates[3] -= 2*size;
	}

	xl = coordinates[0]; xr = coordinates[1]; yl = coordinates[2]; yr = coordinates[3];
	Element* left_element = new Element(coordinates,neighbours,TOP_LEFT);
	CreateTiers((to_create - 2) / 2, (element_id+1)*2, size, coordinates, f, false);
	coordinates[0] = xl; coordinates[1] = xr; coordinates[2] = yl; coordinates[3] = yr;
	coordinates[0] += size;
	coordinates[1] += size;
	Element* right_element = new Element(coordinates,neighbours,TOP_RIGHT);
	CreateTiers((to_create - 2) / 2, (element_id+1)*2 + 2, size, coordinates, f, false);
	elements[element_id] = left_element;
	elements[element_id+1] = right_element;
	tier_vector->push_back(new Tier(left_element,f,matrix,rhs));
	tier_vector->push_back(new Tier(right_element,f,matrix,rhs));

}

//if nr_of_tiers = 1 then nr_of_elements = 10 !

std::vector<EquationSystem*>* MatrixGenerator::CreateMatrixAndRhs(TaskDescription& task_description)
{
		tier_vector = new std::vector<EquationSystem*>();


		IDoubleArgFunction* f = new DoubleArgFunctionWrapper(task_description.function);
		double bot_left_x = task_description.x;
		double bot_left_y = task_description.y;
		int nr_of_tiers = task_description.nrOfTiers;
		double size = task_description.size;

		nr_of_elements = 6*pow(2,nr_of_tiers) - 2;
		elements = new Element*[nr_of_elements];

		double coordinates[4];
		coordinates[0] = bot_left_x;
		coordinates[1] = 0;
		coordinates[2] = bot_left_y;
		coordinates[3] = 0;

		matrix_size = 3*pow(2,nr_of_tiers+3) + 2 * nr_of_tiers + 1;
		//rhs = new double[matrix_size]();
		//matrix = new double*[matrix_size];
		//for(int i = 0; i<matrix_size; i++)
			//matrix[i] = new double[matrix_size]();

		CreateTiers(nr_of_elements,0,size,coordinates,f,true);

		set_numbers(elements,0,1 + nr_of_tiers,0);
		std::vector<EquationSystem*>::iterator it_e = tier_vector->begin();
		for(; it_e != tier_vector->end(); ++it_e){
			((Tier*)(*it_e))->InitTier();

		}

		return tier_vector;
}

void MatrixGenerator::checkSolution(std::map<int,double> *solution_map, double (*function)(int dim, ...))
{
	srand(time(NULL));
	IDoubleArgFunction* f = new DoubleArgFunctionWrapper(*function);
	int i = 0;

	bool solution_ok = true;
	while(i < nr_of_elements && solution_ok)
	{
		Element* element = elements[i];
		solution_ok = element->checkSolution(solution_map,f);
		++i;
	}
	if(solution_ok)
		printf("SOLUTION OK\n");
	else
		printf("WRONG SOLUTION\n");

}

