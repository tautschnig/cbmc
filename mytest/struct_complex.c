struct Point
{
  int x;
  int y;
};

struct Address
{
  char first_line[20];
  char second_line[20];
  char city[20];
  char post_code[20];
};

struct Person
{
  int id;
  struct Address addresses[3];
};



int main()
{
  struct Person emps[5];
  emps[1].id = 101;
  emps[1].addresses[0].first_line[0] = '2';
  emps[1].addresses[0].first_line[1] = '8';
  emps[1].addresses[0].first_line[2] = '\0';
  

  //int a[2];
  
  //a[0] = 10;
  //a[1] = 20;
  
  //int i;
  //struct Point p;

  //p.x = 10;
  //p.y = 20;
  
  //i = p.x;
  
  //struct Point pts[2];

  //pts[0].x = 1;
  //pts[0].y = 2;
  
  //pts[1].x = 3; //The original code 
  //pts[1].y = 4; // could 
  //pts[2].x = 5; //   NOT
  //pts[2].y = 6; //     cope from index 1 onwards
  
	return 0;
}
