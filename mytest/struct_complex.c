struct Point
{
  int x;
  int y;
};

int main()
{
  struct Point pts[3];

  pts[0].x = 1;
  pts[0].y = 2;
  pts[1].x = 3  //The original code 
  pts[1].y = 4; // could 
  pts[2].x = 5; //   NOT
  pts[2].y = 6; //     cope from index 1 onwards
  
	return 0;
}
