add_rules("mode.debug", "mode.release")
add_requires("gtest")
add_requires("capstone")
set_languages("c++20")

target("urhook")
    set_kind("static")
    add_files("src/**.cpp")
    add_includedirs("include",{
      public = true
    })
   
target("tests")
    set_kind("binary")
    add_packages("gtest")
    add_packages("capstone")
    add_deps("urhook")
    add_files("src-test/*.cpp")
    add_links("dl")
    add_ldflags("-rdynamic")

