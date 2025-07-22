add_rules("mode.debug", "mode.release")
add_requires("gtest")
add_requires("capstone")
set_languages("c++20")
set_symbols("debug")
target("urhook")
    set_kind("static")
    add_packages("capstone",{
      public = true
    })
    add_files("src/**.cpp")
    add_includedirs("include",{
      public = true
    })
   
target("tests")
    set_kind("binary")
    add_packages("gtest")
    add_deps("urhook")
    add_files("src-test/*.cpp")
    
