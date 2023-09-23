
import os
import sys
import platform
import re

from os.path import join, dirname, basename, isdir, splitext, realpath
from subprocess import Popen, PIPE
from threading import Timer

# Command line arguments:
# 1. Path to folder containing Lua scripts to run
# 2. Path to LuaJ CLI binary

cli_path = sys.argv[2] # Path to LuaJ CLI binary
timeout = 2            # Seconds before killing a test

COLOR_NONE    = "\x1B[0m"
COLOR_RED     = "\x1B[31m"
COLOR_GREEN   = "\x1B[32m"
COLOR_YELLOW  = "\x1B[33m"
COLOR_BLUE    = "\x1B[34m"
COLOR_MAGENTA = "\x1B[35m"
COLOR_CYAN    = "\x1B[36m"
COLOR_WHITE   = "\x1B[37m"
COLOR_BOLD    = "\x1B[1m"

def print_color(color):
	if platform.system() != "Windows":
		sys.stdout.write(color)

# Prints an error message to the standard output
def print_error(message):
	print_color(COLOR_RED)
	sys.stdout.write("[Error] ")
	print_color(COLOR_NONE)
	print(message)

# Runs a test program from its path. Returns the exit code for the process and
# what was written to the standard output. Returns -1 for the error code if the
# process timed out
def run_test(path):
	# Create the process
	proc = Popen([cli_path, path], stdin=PIPE, stdout=PIPE, stderr=PIPE)

	# Kill test cases that take longer than `timeout` seconds
	timed_out = [False]
	def kill(test_case):
		timed_out[0] = True
		test_case.kill()
	timer = Timer(timeout, kill, [proc])

	# Execute the test case
	exit_code = -1
	output = None
	error = None
	try:
		timer.start()
		output, error = proc.communicate()
		if not timed_out[0]:
			exit_code = proc.returncode
	finally:
		timer.cancel()

	return (output, error, exit_code)

# Validates the output of a test case, returning true if the test was
# successful
def validate(path, output, error, exit_code):
	# Check if the test case timed out
	if exit_code == -1:
		print_error("Timed out")
		return False

	# Check if the test case returned an error
	if exit_code != 0:
		print_error("Exited with error code " + str(exit_code))
		if len(output) > 0:
			print(output.decode("ascii"))
		if len(error) > 0:
			print(error.decode("ascii"))
		return False

	# Print passed test case message
	print_color(COLOR_GREEN)
	sys.stdout.write("[Passed]")
	print_color(COLOR_NONE)
	print("")
	return True

# Executes the runtime test at `path`
def test(path):
	print_color(COLOR_BLUE)
	sys.stdout.write("[Test] ")
	print_color(COLOR_NONE)

	suite = basename(dirname(path))
	name = splitext(basename(path))[0].replace("_", " ")
	print("Testing " + suite + " -> " + name)

	input_file = open(path, "r")
	if not input_file:
		print_error("Failed to open file")
		return False

	source = input_file.read()
	input_file.close()

	output, error, exit_code = run_test(path)
	return validate(path, output, error, exit_code)

# Tests all Lua files in a directory. Returns the total number of tests and the
# number of tests passed
def test_dir(path):
	total = 0
	passed = 0
	files = os.listdir(path)
	for case in files:
		case_path = join(path, case)
		if isdir(case_path):
			local_total, local_passed = test_dir(case_path)
			total += local_total
			passed += local_passed
		elif splitext(case_path)[1] == ".lua":
			if test(case_path):
				passed += 1
			total += 1
	return (total, passed)

total, passed = test_dir(sys.argv[1])
if total > 0:
	print("") # Add a newline

if total == passed:
	print_color(COLOR_GREEN)
	sys.stdout.write("[Success] ")
	print_color(COLOR_NONE)
	print("All tests passed!")
	sys.exit(0)
else:
	print_color(COLOR_RED)
	sys.stdout.write("[Failure] ")
	print_color(COLOR_NONE)
	print(str(passed) + " of " + str(total) + " tests passed")
	sys.exit(1)
