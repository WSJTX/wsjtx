add_test(
        NAME test_jtty_pack
        COMMAND test_jtty_pack
)

add_test(NAME test_jtty_pack_match_output
        COMMAND test_jtty_pack
)
set_tests_properties(test_jtty_pack_match_output PROPERTIES
        PASS_REGULAR_EXPRESSION "^ 1 14    NOW IS THE TIME FOR ALL GOOD MEN TO COME TO THE AID OF THEIR PARTY\n"
        PASS_REGULAR_EXPRESSION "^ 2  9    THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.\n"
        PASS_REGULAR_EXPRESSION "\nTotal messages: 18   Number of errors:  0\n$"
)

configure_file(jtty_msgs.txt "${CMAKE_CURRENT_BINARY_DIR}/jtty_msgs.txt" COPYONLY)

