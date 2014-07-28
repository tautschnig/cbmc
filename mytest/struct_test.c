struct Coordinate
{
   int x;
   int y;
};

struct Employee
{
   int id;
   struct Coordinate location[3];
   struct
   {
      char first_name[20];
      char last_name[20];
   } name;
   
   int salary;
};

int main()
{
  struct Coordinate p;
  struct Employee emp;
  
  p.x = 10;
  p.y = 20;
  
  emp.id = 15;
  
  emp.name.first_name[0] = 'S';
  emp.name.first_name[1] = 'i';
  emp.name.first_name[2] = 'q';
  emp.name.first_name[3] = 'i';
  emp.name.first_name[4] = 'n';
  emp.name.first_name[5] = 'g';
  emp.name.first_name[6] = '\0';
  
  emp.name.last_name[0] = 'T';
  emp.name.last_name[1] = 'A';
  emp.name.last_name[2] = 'N';
  emp.name.last_name[3] = 'G';
  emp.name.last_name[4] = '\0';
  
  emp.salary = 10000;
  
  return 0;
}