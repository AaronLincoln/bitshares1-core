#!/usr/bin/env bash

for regression_test in `ls regression_tests | grep -v "^\\_"`; do
    printf " Running $regression_test                            \\r"
    ./wallet_tests -t regression_tests_without_network/$regression_test > /dev/null 2>&1 || printf "Test $regression_test failed.\\n"
done;
printf "                                                                            \\r"
