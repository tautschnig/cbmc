struct Point
{
  int x;
  int y;
};

struct Address
{
  char first_line[20];
  // char second_line[20];
  // char city[20];
  // char post_code[20];
};

struct Employee
{
  int id;
  struct Address addresses[2];
};

int main(int argc, char **argv)
{
  int a[2];             // 1. array

  struct Point pt;      // 2. struct
  struct Point pts[2];  // 3. array of Point 
  
  struct Employee e;        // 4. e.addresses[1].first_line
  struct Employee emps[2];  // 5. emps[1].addresses[1].first_line

  a[0] = 101;
  a[1] = 102;
  
  pt.x = 10;
  pt.y = 20;

  pts[0].x = 10;
  pts[0].y = 20;
  pts[1].x = 30;
  pts[1].y = 40;
  
  e.id = 1001;
  e.addresses[0].first_line[0] = '2';
  e.addresses[0].first_line[1] = '8';
  e.addresses[0].first_line[2] = '\0';
  e.addresses[1].first_line[0] = '\0';
  
  emps[1].id = 1002;
  emps[1].addresses[0].first_line[0] = '1';
  emps[1].addresses[0].first_line[1] = '5';
  emps[1].addresses[0].first_line[2] = 'A';
  emps[1].addresses[0].first_line[3] = '\0';
  emps[1].addresses[1].first_line[0] = '\0';
  
	return 123;
}
