/** @file   json_utils.h
  * @author Kareem Khazem <karkhaz@karkhaz.com>
  * @date   2014
  * @brief  Pretty output formatting
  */
#ifndef CPROVER_JSON_UTILS
#define CPROVER_JSON_UTILS

#include <sstream>
#include <cassert>

/** @brief Pretty output formatting, e.g. for JSON or XML
 *
 * Objects of this class memorize indentation levels so that you don't
 * have to. They can be used to help output structured information.
 *
 * Example:
 *    
 *     std::stringstream ss;
 *     json_utilt f(2);
 *     ss << "indent level 0" << f.ind();
 *     ss << "these items are";
 *     ss << f.nl()"all on the same";
 *     ss << f.nl()"level of indentation" << f.ind();
 *     ss << "indented yet again";
 *     ss << f.und() << "now back to level 1";
 *     ss << f.und() << "and finally to level 0";
 *     ss << f.nl() << "...";
 *     std::cout << ss.str();
 *
 * Produces the output:
 *      
 *     indent level 0
 *       these items are
 *       all on the same
 *       level of indentation
 *         indented yet again
 *       now back to level 1
 *     and finally to level 0
 *     ...
 *
 * The convention of placing `ind()` at the end of the line, and
 * `nl()` and `und()` at the beginning of the line, makes it easier to
 * change code later.
 */
class json_utilt{
  private: 
    const int indent_width; // int (not unsigned) so that we detect
    int indent_level;       // negatives.

  public:
    json_utilt(): indent_width(2), indent_level(0) {}
    json_utilt(int _indent_width):
      indent_width(_indent_width), indent_level(0) {}

    /**@brief newline, plus increase indentation
     *
     * Style note: put the ind() just after where you want the line
     * to be broken, like ss << "end of line here" << ind();
     */
    std::string ind()
    {    
      assert(indent_level >= 0);
      std::stringstream ss;
      indent_level += 1;
      ss << "\n";
      for(int i = 0; 
          i < (indent_level * indent_width);
          i++) 
        ss << " "; 

      return ss.str();
    }

    /**@brief newline, plus decrease indentation
     *
     * @throws Fails if the indent level is already zero.
     *
     * Style note: put the und() just BEFORE the start of the new
     * line, like ss << und() << "start of new line";
     */
    std::string und()
    {    
      assert(indent_level >= 1);
      indent_level -= 1;
      std::stringstream ss;
      ss << "\n";
      for(int i = 0; 
          i < (indent_level * indent_width);
          i++) 
        ss << " "; 

      return ss.str();
    }    

    /**@brief newline, same indentation as before
     *
     * Style note: put the nl() just BEFORE the start of the new
     * line, like ss << nl() << "start of new line";
     */
    std::string nl() 
    {    
      assert(indent_level >= 0);
      std::stringstream ss;
      ss << "\n";
      for(int i = 0; 
          i < (indent_level * indent_width);
          i++) 
        ss << " "; 

      return ss.str();
    }
};


#endif
