#include "Element.hxx"
using namespace D3;
void set_big_interface_lower_vertex_edge_nrs_first_tier(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::vertex_bot_left_near,nr++);
	left_near_element->set_node_nr(Element::edge_bot_left,nr++);
	left_near_element->set_node_nr(Element::vertex_bot_left_far,nr);
	left_far_element->set_node_nr(Element::vertex_bot_left_near,nr++);
	left_far_element->set_node_nr(Element::edge_bot_left,nr++);
	left_far_element->set_node_nr(Element::vertex_bot_left_far,nr++);
	left_far_element->set_node_nr(Element::edge_bot_far,nr++);
	left_far_element->set_node_nr(Element::vertex_bot_right_far,nr);
	right_far_element->set_node_nr(Element::vertex_bot_left_far,nr++);
	right_far_element->set_node_nr(Element::edge_bot_far,nr++);
	right_far_element->set_node_nr(Element::vertex_bot_right_far,nr);
	//*p_nr += 9;
}
void set_big_interface_upper_vertex_edge_nrs_first_tier(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::vertex_top_left_near,nr++);
	left_near_element->set_node_nr(Element::edge_top_left,nr++);
	left_near_element->set_node_nr(Element::vertex_top_left_far,nr);
	left_far_element->set_node_nr(Element::vertex_top_left_near,nr++);
	left_far_element->set_node_nr(Element::edge_top_left,nr++);
	left_far_element->set_node_nr(Element::vertex_top_left_far,nr++);
	left_far_element->set_node_nr(Element::edge_top_far,nr++);
	left_far_element->set_node_nr(Element::vertex_top_right_far,nr);
	right_far_element->set_node_nr(Element::vertex_top_left_far,nr++);
	right_far_element->set_node_nr(Element::edge_top_far,nr++);
	right_far_element->set_node_nr(Element::vertex_top_right_far,nr);
	//*p_nr += 9;
}
void set_big_interface_edge_face_nrs_first_tier(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::edge_left_near,nr++);
	left_near_element->set_node_nr(Element::face_left,nr++);
	left_near_element->set_node_nr(Element::edge_left_far,nr);
	left_far_element->set_node_nr(Element::edge_left_near,nr++);
	left_far_element->set_node_nr(Element::face_left,nr++);
	left_far_element->set_node_nr(Element::edge_left_far,nr++);
	left_far_element->set_node_nr(Element::face_far,nr++);
	left_far_element->set_node_nr(Element::edge_right_far,nr);
	right_far_element->set_node_nr(Element::edge_left_far,nr++);
	right_far_element->set_node_nr(Element::face_far,nr++);
	right_far_element->set_node_nr(Element::edge_right_far,nr);
	//*p_nr+=9;
}
void set_big_interface_vertex_edge_face_nrs_first_tier(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element, Element* right_near_element)
{
	left_near_element->set_node_nr(Element::edge_top_near,nr++);
	left_near_element->set_node_nr(Element::face_top,nr++);
	left_near_element->set_node_nr(Element::edge_top_far,nr);
	left_far_element->set_node_nr(Element::edge_top_near,nr++);
	left_far_element->set_node_nr(Element::face_top,nr++);
	left_far_element->set_node_nr(Element::edge_top_right,nr);
	right_far_element->set_node_nr(Element::edge_top_left,nr++);
	right_far_element->set_node_nr(Element::face_top,nr++);
	right_far_element->set_node_nr(Element::edge_top_right,nr++);
	left_near_element->set_node_nr(Element::vertex_top_right_near,nr);
	right_near_element->set_node_nr(Element::vertex_top_left_near,nr++);
	left_near_element->set_node_nr(Element::edge_top_right,nr);
	right_near_element->set_node_nr(Element::edge_top_left,nr++);
	left_near_element->set_node_nr(Element::vertex_top_right_far,nr);
	left_far_element->set_node_nr(Element::vertex_top_right_near,nr);
	right_far_element->set_node_nr(Element::vertex_top_left_near,nr);
	right_near_element->set_node_nr(Element::vertex_top_left_far,nr++);
	right_far_element->set_node_nr(Element::edge_top_near,nr);
	right_near_element->set_node_nr(Element::edge_top_far,nr++);
	right_far_element->set_node_nr(Element::vertex_top_right_near,nr);
	right_near_element->set_node_nr(Element::vertex_top_right_far,nr++);
	right_near_element->set_node_nr(Element::edge_top_near,nr++);
	right_near_element->set_node_nr(Element::face_top,nr++);
	right_near_element->set_node_nr(Element::edge_top_right,nr++);
	right_near_element->set_node_nr(Element::vertex_top_right_near,nr++);
	//*p_nr+=16;
}
void set_small_interface_lower_vertex_edge_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element, Element* right_near_elemnt)
{
	bool lower_tier = (right_near_elemnt == NULL);
	left_near_element->set_node_nr(Element::vertex_bot_right_near,nr++);
	if(!lower_tier)
		right_near_elemnt->set_node_nr(Element::vertex_bot_left_near,nr - 1);
	left_near_element->set_node_nr(Element::edge_bot_right,nr++);
	if(!lower_tier)
		right_near_elemnt->set_node_nr(Element::edge_bot_left,nr - 1);
	left_near_element->set_node_nr(Element::vertex_bot_right_far,nr);
	left_far_element->set_node_nr(Element::vertex_bot_right_near,nr);
	right_far_element->set_node_nr(Element::vertex_bot_left_near,nr++);
	if(!lower_tier)
		right_near_elemnt->set_node_nr(Element::vertex_bot_left_far,nr - 1);
	right_far_element->set_node_nr(Element::edge_bot_near,nr++);
	if(!lower_tier)
		right_near_elemnt->set_node_nr(Element::edge_bot_far,nr - 1);
	right_far_element->set_node_nr(Element::vertex_bot_right_near,nr);
	if(!lower_tier)
		right_near_elemnt->set_node_nr(Element::vertex_bot_right_far,nr);
	//*p_nr+=5;
}
void set_small_interface_upper_vertex_edge_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::vertex_top_right_near,nr++);
	left_near_element->set_node_nr(Element::edge_top_right,nr++);
	left_near_element->set_node_nr(Element::vertex_top_right_far,nr);
	left_far_element->set_node_nr(Element::vertex_top_right_near,nr);
	right_far_element->set_node_nr(Element::vertex_top_left_near,nr++);
	right_far_element->set_node_nr(Element::edge_top_near,nr++);
	right_far_element->set_node_nr(Element::vertex_top_right_near,nr++);
	//*p_nr+=5;
}
void set_small_interface_vertex_edge_face_nr(int nr, Element* top_right_near_element)
{
	top_right_near_element->set_node_nr(Element::edge_bot_near,nr++);
	top_right_near_element->set_node_nr(Element::face_bot,nr++);
	top_right_near_element->set_node_nr(Element::edge_bot_right,nr++);
	top_right_near_element->set_node_nr(Element::vertex_bot_right_near,nr++);
	//*p_nr+=4;
}
void set_small_interface_edge_face_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::edge_right_near,nr++);
	left_near_element->set_node_nr(Element::face_right,nr++);
	left_near_element->set_node_nr(Element::edge_right_far,nr);
	left_far_element->set_node_nr(Element::edge_right_near,nr);
	right_far_element->set_node_nr(Element::edge_left_near,nr++);
	right_far_element->set_node_nr(Element::face_near,nr++);
	right_far_element->set_node_nr(Element::edge_right_near,nr++);
	//*p_nr += 5;
}
void set_big_interface_lower_vertex_edge_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::vertex_bot_left_near,nr++);
	left_near_element->set_node_nr(Element::edge_bot_left,nr++);
	left_near_element->set_node_nr(Element::vertex_bot_left_far,nr);
	left_far_element->set_node_nr(Element::vertex_bot_left_near,nr - 2);
	left_far_element->set_node_nr(Element::edge_bot_left,nr - 1);
	left_far_element->set_node_nr(Element::vertex_bot_left_far,nr++);
	left_far_element->set_node_nr(Element::edge_bot_far,nr++);
	left_far_element->set_node_nr(Element::vertex_bot_right_far,nr);
	right_far_element->set_node_nr(Element::vertex_bot_left_far,nr - 2);
	right_far_element->set_node_nr(Element::edge_bot_far,nr - 1);
	right_far_element->set_node_nr(Element::vertex_bot_right_far,nr);
	//*p_nr += 5;
}
void set_big_interface_upper_vertex_edge_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::vertex_top_left_near,nr++);
	left_near_element->set_node_nr(Element::edge_top_left,nr++);
	left_near_element->set_node_nr(Element::vertex_top_left_far,nr);
	left_far_element->set_node_nr(Element::vertex_top_left_near,nr - 2);
	left_far_element->set_node_nr(Element::edge_top_left,nr - 1);
	left_far_element->set_node_nr(Element::vertex_top_left_far,nr++);
	left_far_element->set_node_nr(Element::edge_top_far,nr++);
	left_far_element->set_node_nr(Element::vertex_top_right_far,nr);
	right_far_element->set_node_nr(Element::vertex_top_left_far,nr - 2);
	right_far_element->set_node_nr(Element::edge_top_far,nr - 1);
	right_far_element->set_node_nr(Element::vertex_top_right_far,nr);
	//*p_nr += 5;
}
void set_big_interface_edge_face_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::edge_left_near,nr++);
	left_near_element->set_node_nr(Element::face_left,nr++);
	left_near_element->set_node_nr(Element::edge_left_far,nr);
	left_far_element->set_node_nr(Element::edge_left_near,nr - 2);
	left_far_element->set_node_nr(Element::face_left,nr - 1);
	left_far_element->set_node_nr(Element::edge_left_far,nr++);
	left_far_element->set_node_nr(Element::face_far,nr++);
	left_far_element->set_node_nr(Element::edge_right_far,nr);
	right_far_element->set_node_nr(Element::edge_left_far,nr - 2);
	right_far_element->set_node_nr(Element::face_far,nr - 1);
	right_far_element->set_node_nr(Element::edge_right_far,nr);
	//*p_nr+=5;
}
void set_big_interface_vertex_edge_face_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element, Element* right_near_element)
{
	left_near_element->set_node_nr(Element::edge_top_near,nr + 0);
	left_near_element->set_node_nr(Element::face_top,nr + 1);
	left_near_element->set_node_nr(Element::edge_top_far,nr - 2);
	left_far_element->set_node_nr(Element::edge_top_near,nr + 0);
	left_far_element->set_node_nr(Element::face_top,nr + 1);
	left_far_element->set_node_nr(Element::edge_top_right,nr + 2);
	right_far_element->set_node_nr(Element::edge_top_left,nr - 4);
	right_far_element->set_node_nr(Element::face_top,nr + 1);
	right_far_element->set_node_nr(Element::edge_top_right,nr + 2);
	left_near_element->set_node_nr(Element::vertex_top_right_near,nr + 3);
	right_near_element->set_node_nr(Element::vertex_top_left_near,nr - 5);
	left_near_element->set_node_nr(Element::edge_top_right,nr + 2);
	right_near_element->set_node_nr(Element::edge_top_left,nr - 4);
	left_near_element->set_node_nr(Element::vertex_top_right_far,nr - 1);
	left_far_element->set_node_nr(Element::vertex_top_right_near,nr + 3);
	right_far_element->set_node_nr(Element::vertex_top_left_near,nr - 5);
	right_near_element->set_node_nr(Element::vertex_top_left_far,nr - 3);
	right_far_element->set_node_nr(Element::edge_top_near,nr);
	right_near_element->set_node_nr(Element::edge_top_far,nr - 2);
	right_far_element->set_node_nr(Element::vertex_top_right_near,nr + 3);
	right_near_element->set_node_nr(Element::vertex_top_right_far,nr - 1);
	right_near_element->set_node_nr(Element::edge_top_near,nr);
	right_near_element->set_node_nr(Element::face_top,nr + 1);
	right_near_element->set_node_nr(Element::edge_top_right,nr + 2);
	right_near_element->set_node_nr(Element::vertex_top_right_near,nr + 3);
	//*p_nr+=4;
}
void set_internal_lower_edge_face_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::edge_bot_near,nr++);
	left_near_element->set_node_nr(Element::face_bot,nr++);
	left_near_element->set_node_nr(Element::edge_bot_far,nr);
	left_far_element->set_node_nr(Element::edge_bot_near,nr++);
	left_far_element->set_node_nr(Element::face_bot,nr++);
	left_far_element->set_node_nr(Element::edge_bot_right,nr);
	right_far_element->set_node_nr(Element::edge_bot_left,nr++);
	right_far_element->set_node_nr(Element::face_bot,nr++);
	right_far_element->set_node_nr(Element::edge_bot_right,nr);
	//*p_nr+=7;
}
void set_internal_upper_edge_face_nrs(int nr,
		Element* left_near_element, Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::edge_top_near,nr++);
	left_near_element->set_node_nr(Element::face_top,nr++);
	left_near_element->set_node_nr(Element::edge_top_far,nr);
	left_far_element->set_node_nr(Element::edge_top_near,nr++);
	left_far_element->set_node_nr(Element::face_top,nr++);
	left_far_element->set_node_nr(Element::edge_top_right,nr);
	right_far_element->set_node_nr(Element::edge_top_left,nr++);
	right_far_element->set_node_nr(Element::face_top,nr++);
	right_far_element->set_node_nr(Element::edge_top_right,nr);
	//*p_nr+=7;
}
void set_internal_face_interior_nrs(int nr,
		Element* left_near_element,	Element* left_far_element, Element* right_far_element)
{
	left_near_element->set_node_nr(Element::face_near,nr++);
	left_near_element->set_node_nr(Element::interior,nr++);
	left_near_element->set_node_nr(Element::face_far,nr);
	left_far_element->set_node_nr(Element::face_near,nr++);
	left_far_element->set_node_nr(Element::interior,nr++);
	left_far_element->set_node_nr(Element::face_right,nr);
	right_far_element->set_node_nr(Element::face_left,nr++);
	right_far_element->set_node_nr(Element::interior,nr++);
	right_far_element->set_node_nr(Element::face_right,nr);
	//*p_nr+=7;
}
void set_internal_top_right_near_edge_face_interior_nrs(int nr,
		Element* left_near_element,	Element* left_far_element, Element* right_far_element, Element* right_near_element)
{
	left_near_element->set_node_nr(Element::edge_right_near,nr);
	right_near_element->set_node_nr(Element::edge_left_near,nr++);
	left_near_element->set_node_nr(Element::face_right,nr);
	right_near_element->set_node_nr(Element::face_left,nr++);
	left_near_element->set_node_nr(Element::edge_right_far,nr);
	left_far_element->set_node_nr(Element::edge_right_near,nr);
	right_far_element->set_node_nr(Element::edge_left_near,nr);
	right_near_element->set_node_nr(Element::edge_left_far,nr++);
	right_far_element->set_node_nr(Element::face_near,nr);
	right_near_element->set_node_nr(Element::face_far,nr++);
	right_far_element->set_node_nr(Element::edge_right_near,nr);
	right_near_element->set_node_nr(Element::edge_right_far,nr++);

	right_near_element->set_node_nr(Element::face_near,nr++);

	right_near_element->set_node_nr(Element::interior,nr++);
	right_near_element->set_node_nr(Element::face_right,nr++);
	right_near_element->set_node_nr(Element::edge_right_near,nr);
	//*p_nr+=9;
}


Element** Element::CreateAnotherTier(int nr)
{
	Element* bot_left_near_element = new Element(xl,yl - size, zl, size,BOT_LEFT_NEAR,false);
	Element* bot_right_far_element = new Element(xr,yl,zl,size,BOT_RIGHT_FAR,false);
	Element* top_left_near_element = new Element(xl,yl - size, zl + size, size,TOP_LEFT_NEAR,false);
	Element* top_right_far_element = new Element(xr,yl,zl+size,size,TOP_RIGHT_FAR,false);
	Element* top_left_far_element = new Element(xl,yl,zl+size,size,TOP_LEFT_FAR,false);
	Element* top_right_near_element = new Element(xl+size,yl-size,zl+size,size,TOP_RIGHT_NEAR,false);


	set_big_interface_lower_vertex_edge_nrs(nr,bot_left_near_element,this,bot_right_far_element);
	nr+=5;
	set_big_interface_edge_face_nrs(nr,bot_left_near_element,this,bot_right_far_element);
	nr+=5;
	set_big_interface_upper_vertex_edge_nrs(nr,bot_left_near_element,this,bot_right_far_element);
	nr-=10;
	set_big_interface_lower_vertex_edge_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=5;
	set_big_interface_edge_face_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=5;
	set_big_interface_upper_vertex_edge_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=5;
	set_big_interface_vertex_edge_face_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_element,top_right_near_element);
	nr+=4;
	SetIternalBotInterfaceNumbers(nr,bot_left_near_element,this,bot_right_far_element,
			top_left_near_element, top_left_far_element,top_right_far_element,top_right_near_element);
	nr+=37;


	Element** elements = new Element*[7];
	elements[0] = bot_left_near_element;
	elements[1] = this;
	elements[2] = bot_right_far_element;
	elements[3] = top_left_near_element;
	elements[4] = top_left_far_element;
	elements[5] = top_right_far_element;
	elements[6] = top_right_near_element;

	return elements;
}


Element** Element::CreateFirstTier(int nr){

	Element* bot_left_near_element = new Element(xl,yl - size, zl, size,BOT_LEFT_NEAR, true);
	Element* bot_right_far_element = new Element(xr,yl,zl,size,BOT_RIGHT_FAR,true);
	Element* top_left_near_element = new Element(xl,yl - size, zl + size, size,TOP_LEFT_NEAR, true);
	Element* top_right_far_element = new Element(xr,yl,zl+size,size,TOP_RIGHT_FAR,true);
	Element* top_left_far_element = new Element(xl,yl,zl+size,size,TOP_LEFT_FAR,true);
	Element* top_right_near_element = new Element(xl+size,yl-size,zl+size,size,TOP_RIGHT_NEAR,true);

	set_big_interface_lower_vertex_edge_nrs_first_tier(nr,bot_left_near_element,this,bot_right_far_element);
	nr+=9;
	set_big_interface_edge_face_nrs_first_tier(nr,bot_left_near_element,this,bot_right_far_element);
	nr+=9;
	set_big_interface_upper_vertex_edge_nrs_first_tier(nr,bot_left_near_element,this,bot_right_far_element);
	set_big_interface_lower_vertex_edge_nrs_first_tier(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=9;
	set_big_interface_edge_face_nrs_first_tier(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=9;
	set_big_interface_upper_vertex_edge_nrs_first_tier(nr,top_left_near_element,top_left_far_element,top_right_far_element);
	nr+=9;
	set_big_interface_vertex_edge_face_nrs_first_tier(nr,top_left_near_element,top_left_far_element,top_right_far_element,top_right_near_element);
	nr+=16;
	SetIternalBotInterfaceNumbers(nr,bot_left_near_element,this,bot_right_far_element,
			top_left_near_element, top_left_far_element,top_right_far_element,top_right_near_element);
	nr+=37;
		

	Element** elements = new Element*[7];
	elements[0] = bot_left_near_element;
	elements[1] = this;
	elements[2] = bot_right_far_element;
	elements[3] = top_left_near_element;
	elements[4] = top_left_far_element;
	elements[5] = top_right_far_element;
	elements[6] = top_right_near_element;

	return elements;
}


Element** Element::CreateLastTier(int nr)
{	
		shapeFunctionNrs[vertex_bot_left_near] = nr++;
		shapeFunctionNrs[edge_bot_left] = nr++;
		shapeFunctionNrs[vertex_bot_left_far] = nr++;
		shapeFunctionNrs[edge_bot_far] = nr++;
		shapeFunctionNrs[vertex_bot_right_far] = nr++;
		shapeFunctionNrs[edge_left_near] = nr++;
		shapeFunctionNrs[face_left] = nr++;
		shapeFunctionNrs[edge_left_far] = nr++;
		shapeFunctionNrs[face_far] = nr++;
		shapeFunctionNrs[edge_right_far] = nr++;
		shapeFunctionNrs[vertex_top_left_near] = nr++;
		shapeFunctionNrs[edge_top_left] = nr++;
		shapeFunctionNrs[vertex_top_left_far] = nr++;
		shapeFunctionNrs[edge_top_far] = nr++;
		shapeFunctionNrs[vertex_top_right_far] = nr++;
		shapeFunctionNrs[edge_top_near] = nr++;
		shapeFunctionNrs[face_top] = nr++;
		shapeFunctionNrs[edge_top_right] = nr++;
		shapeFunctionNrs[vertex_top_right_near] = nr++;
		shapeFunctionNrs[edge_bot_near] = nr++;
		shapeFunctionNrs[face_bot] = nr++;
		shapeFunctionNrs[edge_bot_right] = nr++;
		shapeFunctionNrs[face_near] = nr++;
		shapeFunctionNrs[interior] = nr++;
		shapeFunctionNrs[face_right]= nr++;
		shapeFunctionNrs[vertex_bot_right_near] = nr++;
		shapeFunctionNrs[edge_right_near] = nr++;

		return NULL;
}

void Element::SetIternalBotInterfaceNumbers(int nr, Element* bot_left_near_element, Element* bot_left_far_element, Element* bot_right_far_element,
		Element* top_left_near_element, Element* top_left_far_element, Element* top_right_far_elemnt, Element* top_right_near_element)
{
	set_internal_lower_edge_face_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element);
	nr+=7;
	set_internal_face_interior_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element);
	nr+=7;
	set_internal_upper_edge_face_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element);
	set_internal_lower_edge_face_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_elemnt);
	nr+=7;
	set_internal_face_interior_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_elemnt);
	nr+=7;
	set_internal_top_right_near_edge_face_interior_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_elemnt,top_right_near_element);
	nr+=9;//37
	set_small_interface_lower_vertex_edge_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element,NULL);
	nr+=5;
	set_small_interface_edge_face_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element);
	nr+=5;
	set_small_interface_upper_vertex_edge_nrs(nr,bot_left_near_element,bot_left_far_element,bot_right_far_element);
	set_small_interface_lower_vertex_edge_nrs(nr,top_left_near_element,top_left_far_element,top_right_far_elemnt,top_right_near_element);
	nr+=5;
	set_small_interface_vertex_edge_face_nr(nr,top_right_near_element);
	nr+=4;
}

class DoubleArgFunctionProduct : public ITripleArgFunction
{
	
	private:
		ITripleArgFunction* function1;
		ITripleArgFunction* function2;
		
	public:
		void SetFunctions(ITripleArgFunction* _function1, ITripleArgFunction* _function2)
		{
			function1 = _function1;
			function2 = _function2;
		}
	
		virtual double ComputeValue(double x, double y, double z)
		{
			return function1->ComputeValue(x,y,z)*function2->ComputeValue(x,y,z);
		}
	
};


void Element::comp(int indx1, int indx2, ITripleArgFunction* f1, ITripleArgFunction* f2,double** matrix)
{
		DoubleArgFunctionProduct* product = new DoubleArgFunctionProduct();
		product->SetFunctions(f1,f2);
		matrix[indx1][indx2] += GaussianQuadrature::definiteTripleIntegral(xl, xr, yl, yr, zl, zr, product);
		delete product;
}


void Element::fillMatrix(double** matrix)
{
	fillMatrix(matrix,0);
		
}

void Element::fillMatrix(double** matrix, int start_adj_nr)
{

	for(int i = 0; i<nr_of_nodes; i++){
		for(int j = 0; j<nr_of_nodes; j++){
			comp(shapeFunctionNrs[i] - start_adj_nr, shapeFunctionNrs[j] - start_adj_nr,
					shapeFunctions[i], shapeFunctions[j], matrix);
		}
	}
}

void Element::fillRhs(double* rhs, ITripleArgFunction* f){
	fillRhs(rhs, f, 0);
}

void Element::fillRhs(double* rhs, ITripleArgFunction* f, int start_adj_nr)
{

	for(int i = 0; i<nr_of_nodes; i++){
		DoubleArgFunctionProduct* product = new DoubleArgFunctionProduct();
		product->SetFunctions(shapeFunctions[i], f);
		rhs[shapeFunctionNrs[i] - start_adj_nr] += GaussianQuadrature::definiteTripleIntegral(xl, xr, yl, yr, zl, zr, product);
		delete product;
	}
}

void Element::fillTierMatrix(double** matrix, double* rhs, ITripleArgFunction* f, int start_nr_adj){
		fillMatrix(matrix, start_nr_adj);
		fillRhs(rhs, f, start_nr_adj);
}

bool Element::checkSolution(std::map<int,double> *solution_map, ITripleArgFunction* f)
{
	double coefficients[nr_of_nodes];

	for(int i = 0; i<nr_of_nodes; i++)
		coefficients[i] = solution_map->find(shapeFunctionNrs[i])->second;

	int nr_of_samples = 5;
	double epsilon = 1e-8;


	double rnd_x_within_element;
	double rnd_y_within_element;
	double rnd_z_within_element;

	for(int i = 0; i<nr_of_samples; i++)
	{
		double value = 0;
		double rnd_x_within_element = ((double) rand() / (RAND_MAX))*size + xl;
		double rnd_y_within_element = ((double) rand() / (RAND_MAX))*size + yl;
		double rnd_z_within_element = ((double) rand() / (RAND_MAX))*size + zl;
		for(int i = 0; i<nr_of_nodes; i++)
			value+=coefficients[i]*shapeFunctions[i]->ComputeValue(rnd_x_within_element,rnd_y_within_element,rnd_z_within_element);
		printf("%d %lf Checking at: %lf %lf %lf values: %lf %lf\n",position,size,rnd_x_within_element,rnd_y_within_element,rnd_z_within_element,value,f->ComputeValue(rnd_x_within_element,rnd_y_within_element,rnd_z_within_element));
		if(fabs(value - f->ComputeValue(rnd_x_within_element,rnd_y_within_element,rnd_z_within_element)) > epsilon)
		{
			for(int i = 0; i<nr_of_nodes; i++)
					printf("%.16f\n", coefficients[i]);
			return false;
		}
	}

	return true;
}
