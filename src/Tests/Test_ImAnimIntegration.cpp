#include <catch2/catch_test_macros.hpp>

// This should fail to compile if ImAnim is not set up
#include <im_anim.h> 

TEST_CASE("ImAnim Integration", "[ImAnim]") {
    // Just a basic check to ensure we can link against it
    REQUIRE(true);
}
