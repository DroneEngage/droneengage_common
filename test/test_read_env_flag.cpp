#include <iostream>
#include <cstdlib>
#include <cassert>
#include "../helpers/helpers.hpp"

void test_read_env_flag() {
    std::cout << "Testing read_env_flag function..." << std::endl;
    
    // Test with unset environment variable
    unsetenv("TEST_FLAG_UNSET");
    assert(read_env_flag("TEST_FLAG_UNSET") == false);
    std::cout << "✓ Unset environment variable returns false" << std::endl;
    
    // Test with various true values
    setenv("TEST_FLAG_1", "1", 1);
    assert(read_env_flag("TEST_FLAG_1") == true);
    std::cout << "✓ '1' returns true" << std::endl;
    
    setenv("TEST_FLAG_TRUE", "true", 1);
    assert(read_env_flag("TEST_FLAG_TRUE") == true);
    std::cout << "✓ 'true' returns true" << std::endl;
    
    setenv("TEST_FLAG_TRUE_UPPER", "TRUE", 1);
    assert(read_env_flag("TEST_FLAG_TRUE_UPPER") == true);
    std::cout << "✓ 'TRUE' returns true" << std::endl;
    
    setenv("TEST_FLAG_YES", "yes", 1);
    assert(read_env_flag("TEST_FLAG_YES") == true);
    std::cout << "✓ 'yes' returns true" << std::endl;
    
    setenv("TEST_FLAG_YES_UPPER", "YES", 1);
    assert(read_env_flag("TEST_FLAG_YES_UPPER") == true);
    std::cout << "✓ 'YES' returns true" << std::endl;
    
    setenv("TEST_FLAG_ON", "on", 1);
    assert(read_env_flag("TEST_FLAG_ON") == true);
    std::cout << "✓ 'on' returns true" << std::endl;
    
    setenv("TEST_FLAG_ON_UPPER", "ON", 1);
    assert(read_env_flag("TEST_FLAG_ON_UPPER") == true);
    std::cout << "✓ 'ON' returns true" << std::endl;
    
    // Test with various false values
    setenv("TEST_FLAG_0", "0", 1);
    assert(read_env_flag("TEST_FLAG_0") == false);
    std::cout << "✓ '0' returns false" << std::endl;
    
    setenv("TEST_FLAG_FALSE", "false", 1);
    assert(read_env_flag("TEST_FLAG_FALSE") == false);
    std::cout << "✓ 'false' returns false" << std::endl;
    
    setenv("TEST_FLAG_NO", "no", 1);
    assert(read_env_flag("TEST_FLAG_NO") == false);
    std::cout << "✓ 'no' returns false" << std::endl;
    
    setenv("TEST_FLAG_OFF", "off", 1);
    assert(read_env_flag("TEST_FLAG_OFF") == false);
    std::cout << "✓ 'off' returns false" << std::endl;
    
    setenv("TEST_FLAG_RANDOM", "random", 1);
    assert(read_env_flag("TEST_FLAG_RANDOM") == false);
    std::cout << "✓ 'random' returns false" << std::endl;
    
    setenv("TEST_FLAG_EMPTY", "", 1);
    assert(read_env_flag("TEST_FLAG_EMPTY") == false);
    std::cout << "✓ Empty string returns false" << std::endl;
    
    std::cout << "All tests passed! ✅" << std::endl;
}

int main() {
    test_read_env_flag();
    return 0;
}
