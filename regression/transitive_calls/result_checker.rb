#!/usr/bin/env ruby
# Checks whether the results of running
# goto-instrument --thread-functions on a goto binary match our
# expectations of what functions are called.
# Author: Kareem Khazem

require 'json'

require 'set'

usage = "USAGE\n  result_checker.rb test_name result_file expected_file\n"

if ARGV.length != 3
  printf $stderr, usage
  exit 1
end

$test_name, result_file, expected_file = ARGV
unless (File.exists? result_file) and (File.exists? expected_file)
  printf $stderr, usage
  printf $stderr, "Could not find %s\n",
    unless File.exists? result_file; result_file
    else expected_file end
end

$red = "$(tput setab 1)$(tput setaf 8)"
$yellow = "$(tput setab 3)$(tput setaf 8)"
$green = "$(tput setab 2)$(tput setaf 8)"
$cyan = "$(tput setab 8)$(tput setaf 6)"
$reset = "$(tput sgr 0)"

def eror str                                                                              
  system "printf \"#{$red}eror#{$cyan} #{$test_name}#{$reset}: #{str}\n\" 1>&2"
end

def warn str
  system "printf \"#{$yellow}warn#{$cyan} #{$test_name}#{$reset}: #{str}\n\" 1>&2"
end

def note str
  system "printf \"#{$green}note#{$cyan} #{$test_name}#{$reset}: #{str}\n\" 1>&2"
end

def fun_eql fun1, fun2
  ("c::" + fun1) == fun2 or\
  ("c::" + fun2) == fun1 or\
    fun1 == fun2
end

def compare_result real, expected
  all_ok = true
  fun_name = real['function_name']
  real_calls = real['called_functions']
  expected_calls = expected['called_functions']

  expected_calls.each do |expected_call|
    found = false
    real_calls.each {|r| found = true if fun_eql r, expected_call}
    unless found
      all_ok = false
      eror "Expected #{expected_call} to be called from #{fun_name}"
    end
  end
  real_calls.each do |real_call|
    found = false
    expected_calls.collect {|e| found = true if fun_eql e, real_call}
    unless found
      unless fun_name == "main"
        all_ok = false
        warn "#{real_call} called from #{fun_name} (unexpected)"
      end
    end
  end
  all_ok
end

def compare_results results, expected
  # Add all functions to a set. We want to ensure that main
  # transitively calls everything, even if the expected file didn't
  # specify that.
  all_funs = Set.new

  all_ok = true
  results.each do |r1|
    name1 = r1['function_name']
    all_funs << name1
    expected.each do |r2|
      name2 = r2['function_name']
      all_funs << name2
      if fun_eql name1, name2
        fun_1_match = true
        fun_2_match = true
        all_ok = false unless compare_result r1, r2
      end
    end
  end

  # Now ensure that main transitively called everything
  all_funs = all_funs.to_a
  all_funs.delete "__actual_thread_spawn"
  all_funs.delete "c::__actual_thread_spawn"
  all_funs.delete "c::__CPROVER_initialize"
  all_funs.delete "c::main"
  all_funs.delete "main"
  main_exp = {"function_name"=>"main", "called_functions"=>all_funs}

  main_real = results.select {|i| i["function_name"] == "c::main"}
  (eror main_real; exit 1) if main_real.length != 1
  main_real = main_real[0]

  all_ok = false unless compare_result main_real, main_exp

  all_ok
end

results  = JSON.parse (File.read result_file)
expected = JSON.parse (File.read expected_file)

all_ok = compare_results results, expected
note "All tests passed" if all_ok
