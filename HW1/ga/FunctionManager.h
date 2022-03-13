
#include <functional>
#include <string>
#include <vector>

namespace ga {

class FunctionManager
{
  public:
    FunctionManager(std::string&& functionName, int dimensions);
    double operator()(std::vector<double>& x) const;

  private:
    std::function<double(std::vector<double>&)> initFunction(int dimensions);

    std::string functionName;
    std::function<double(std::vector<double>&)> function;
};

} // namespace ga
