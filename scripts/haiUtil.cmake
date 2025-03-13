#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2018 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# cmake内置模块，用于检查C++源码是否可以编译
include(CheckCXXSourceCompiles)

# Useful for combinging paths

# 函数adddirname: 将给定列表中的每个项与指定的前缀连接起来，并将结果存在在一个输出列表中
# 函数名：adddirname
# 指定的前缀: prifix
# 输入列表： lst
# 输出列表: out_lst
function(adddirname prefix lst out_lst)
	set(output)
	foreach(item ${lst})
		list(APPEND output "${prefix}/${item}")
	endforeach()
	set(${out_lst} ${${out_lst}} ${output} PARENT_SCOPE)
endfunction()

# Splits a version formed as "major.minor.patch" recorded in variable 'prefix'
# and writes it into variables started with 'prefix' and ended with _MAJOR, _MINOR and _PATCH.
# 解析版本号，并将结果存储在以prefix为前缀的变量中
MACRO(set_version_variables prefix value)
	string(REPLACE "." ";" VERSION_LIST ${value})	# 将版本号字符串中的.替换为; 以便将其转为列表形式
	list(GET VERSION_LIST 0 ${prefix}_MAJOR)		# 主版本号major
	list(GET VERSION_LIST 1 ${prefix}_MINOR)		# 次版本号minor
	list(GET VERSION_LIST 2 ${prefix}_PATCH)		# 修订版本号patch
	set(${prefix}_DEFINESTR "")						# 版本号定义字符串,初始化为空
ENDMACRO(set_version_variables)

# Sets given variable to 1, if the condition that follows it is satisfied.
# Otherwise set it to 0.
# 根据给定的条件设置一个变量的值：如果给定的条件为真，则将变量设置为1；否则设置为0。
MACRO(set_if varname)
	IF(${ARGN})
		SET(${varname} 1)
	ELSE(${ARGN})
		SET(${varname} 0)
	ENDIF(${ARGN})
ENDMACRO(set_if)

# 将传入的多个参数连接成一个字符串，并将结果存储在变量outvar中
FUNCTION(join_arguments outvar)
	set (output)

	foreach (i ${ARGN})						# 遍历所有的参数
		set(output "${output} ${i}")		# 追加到output变量中
	endforeach()

	set (${outvar} ${output} PARENT_SCOPE)	# 将最终结果保存到输出变量outvar中
ENDFUNCTION()

# The directory specifies the location of maffile and
# all files specified in the list.
# 读取指定目录下的MAFF文件，并解析其各个部分
MACRO(MafReadDir directory maffile)
	# ARGN contains the extra "section-variable" pairs
	# If empty, return nothing
	# 定义可接受的标签列表
	set (MAFREAD_TAGS
		SOURCES            # source files
		PUBLIC_HEADERS     # installable headers for include
		PROTECTED_HEADERS  # installable headers used by other headers
		PRIVATE_HEADERS    # non-installable headers
		SOURCES_WIN32_SHARED	# windows specific SOURCES
		PRIVATE_HEADERS_WIN32_SHARED	# windows specific PRIVATE_HEADERS
		OPTIONS
	)

	# 解析传入的参数
	cmake_parse_arguments(MAFREAD_VAR "" "${MAFREAD_TAGS}" "" ${ARGN})
	# Arguments for these tags are variables to be filled
	# with the contents of particular section.
	# While reading the file, extract the section.
	# Section is recognized by either first uppercase character or space.

	# @c http://cmake.org/pipermail/cmake/2007-May/014222.html
	# 获取MAFF文件内容
	FILE(READ ${directory}/${maffile} MAFREAD_CONTENTS)
	STRING(REGEX REPLACE ";" "\\\\;" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")
	STRING(REGEX REPLACE "\n" ";" MAFREAD_CONTENTS "${MAFREAD_CONTENTS}")

	# Once correctly read, declare this file as dependency of the build file.
	# Normally you should use cmake_configure_depends(), but this is
	# available only since 3.0 version.

	# 将文件设置为构建文件的依赖项
	configure_file(${directory}/${maffile} dummy_${maffile}.cmake.out)
	file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/dummy_${maffile}.cmake.out)

	#message("DEBUG: MAF FILE CONTENTS: ${MAFREAD_CONTENTS}")
	#message("DEBUG: PASSED VARIABLES:")
	#foreach(DEBUG_VAR ${MAFREAD_TAGS})
	#	message("DEBUG: ${DEBUG_VAR}=${MAFREAD_VAR_${DEBUG_VAR}}")
	#endforeach()

	# The unnamed section becomes SOURCES

	# 初始化变量
	set (MAFREAD_VARIABLE ${MAFREAD_VAR_SOURCES})
	set (MAFREAD_UNASSIGNED "")

	# Default section type. Another is 'flags'.

	# 默认节类型
	set (MAFREAD_SECTION_TYPE file)

	# 逐行遍历文件内容
	FOREACH(MAFREAD_LINE ${MAFREAD_CONTENTS})
		# Test what this line is
		string(STRIP ${MAFREAD_LINE} MAFREAD_OLINE)
		string(SUBSTRING ${MAFREAD_OLINE} 0 1 MAFREAD_FIRST)
		#message("DEBUG: LINE='${MAFREAD_LINE}' FIRST='${MAFREAD_FIRST}'")

		# The 'continue' command is cmake 3.2 - very late discovery

		# 检查是否为空行
		if (MAFREAD_FIRST STREQUAL "")
			#message("DEBUG: ... skipped: empty")
		# 检查是否为注释行
		elseif (MAFREAD_FIRST STREQUAL "#")
			#message("DEBUG: ... skipped: comment")
		else()
			# Will be skipped if the line was a comment/empty

			# 检查是否为节的开始
			string(REGEX MATCH "[ A-Z-]" MAFREAD_SECMARK ${MAFREAD_FIRST})
			if (MAFREAD_SECMARK STREQUAL "")
				# This isn't a section, it's a list element.
				#message("DEBUG: ITEM: ${MAFREAD_OLINE} --> ${MAFREAD_VARIABLE}")

				# 文本行
				if (${MAFREAD_SECTION_TYPE} STREQUAL file)
					get_filename_component(MAFREAD_OLINE ${directory}/${MAFREAD_OLINE} ABSOLUTE)
				endif()

				set (MAFREAD_CONDITION_OK 1)
				if (DEFINED MAFREAD_CONDITION_LIST)
					FOREACH(MFITEM IN ITEMS ${MAFREAD_CONDITION_LIST})
						separate_arguments(MFITEM)
						FOREACH(MFVAR IN ITEMS ${MFITEM})
							STRING(SUBSTRING ${MFVAR} 0 1 MFPREFIX)
							if (MFPREFIX STREQUAL "!")
								STRING(SUBSTRING ${MFVAR} 1 -1 MFVAR)
								if (${MFVAR})
									set (MFCONDITION_RESULT 0)
								else()
									set (MFCONDITION_RESULT 1)
								endif()
							else()
								if (${MFVAR})
									set (MFCONDITION_RESULT 1)
								else()
									set (MFCONDITION_RESULT 0)
								endif()
							endif()
							#message("CONDITION: ${MFPREFIX} ${MFVAR} -> ${MFCONDITION_RESULT}")

							MATH(EXPR MAFREAD_CONDITION_OK "${MAFREAD_CONDITION_OK} & (${MFCONDITION_RESULT})")
						ENDFOREACH()
					ENDFOREACH()
				endif()

				if (MAFREAD_CONDITION_OK)
					LIST(APPEND ${MAFREAD_VARIABLE} ${MAFREAD_OLINE})
				else()
					#message("... NOT ADDED ITEM: ${MAFREAD_OLINE}")
				endif()
			else()
				# It's a section
				# Check for conditionals (clear current conditions first)
				unset(MAFREAD_CONDITION_LIST)

				STRING(FIND ${MAFREAD_OLINE} " -" MAFREAD_HAVE_CONDITION)
				if (NOT MAFREAD_HAVE_CONDITION EQUAL -1)
					# Cut off conditional specification, and 
					# grab the section name and condition list
					STRING(REPLACE " -" ";" MAFREAD_CONDITION_LIST ${MAFREAD_OLINE})

					#message("CONDITION READ: ${MAFREAD_CONDITION_LIST}")

					LIST(GET MAFREAD_CONDITION_LIST 0 MAFREAD_OLINE)
					LIST(REMOVE_AT MAFREAD_CONDITION_LIST 0)
					#message("EXTRACTING SECTION=${MAFREAD_OLINE} CONDITIONS=${MAFREAD_CONDITION_LIST}")
				endif()
				# change the running variable
				# Make it section name
				STRING(REPLACE  " " "_" MAFREAD_SECNAME ${MAFREAD_OLINE})
				#message("MAF SECTION: ${MAFREAD_SECNAME}")

				# The cmake's version of 'if (MAFREAD_SECNAME[0] == '-')' - sigh...
				string(SUBSTRING ${MAFREAD_SECNAME} 0 1 MAFREAD_SECNAME0)
				if (${MAFREAD_SECNAME0} STREQUAL "-")
					set (MAFREAD_SECTION_TYPE option)
					string(SUBSTRING ${MAFREAD_SECNAME} 1 -1 MAFREAD_SECNAME)
				else()
					set (MAFREAD_SECTION_TYPE file)
				endif()
				set(MAFREAD_VARIABLE ${MAFREAD_VAR_${MAFREAD_SECNAME}})
				if (MAFREAD_VARIABLE STREQUAL "")
					set(MAFREAD_VARIABLE MAFREAD_UNASSIGNED)
				endif()
				#message("DEBUG: NEW SECTION: '${MAFREAD_SECNAME}' --> VARIABLE: '${MAFREAD_VARIABLE}'")
			endif()
		endif()
	ENDFOREACH()
	
	# Final debug report
	#set (ALL_VARS "")
	#message("DEBUG: extracted variables:")
	#foreach(DEBUG_VAR ${MAFREAD_TAGS})
	#	list(APPEND ALL_VARS ${MAFREAD_VAR_${DEBUG_VAR}})
	#endforeach()
	#list(REMOVE_DUPLICATES ALL_VARS)
	#foreach(DEBUG_VAR ${ALL_VARS})
	#	message("DEBUG: --> ${DEBUG_VAR} = ${${DEBUG_VAR}}")
	#endforeach()
ENDMACRO(MafReadDir)

# NOTE: This is historical only. Not in use.
# It should be a similar interface to mafread.tcl like
# the above MafRead macro.
MACRO(GetMafHeaders directory outvar)
	EXECUTE_PROCESS(
		COMMAND ${CMAKE_MODULE_PATH}/mafread.tcl
			${CMAKE_SOURCE_DIR}/${directory}/HEADERS.maf
			"PUBLIC HEADERS"
			"PROTECTED HEADERS"
		OUTPUT_STRIP_TRAILING_WHITESPACE
		OUTPUT_VARIABLE ${outvar}
	)
	SEPARATE_ARGUMENTS(${outvar})
	adddirname(${CMAKE_SOURCE_DIR}/${directory} "${${outvar}}" ${outvar})
ENDMACRO(GetMafHeaders)

function (getVarsWith _prefix _varResult)
	get_cmake_property(_vars VARIABLES)
	string (REGEX MATCHALL "(^|;)${_prefix}[A-Za-z0-9_]*" _matchedVars "${_vars}")
	set (${_varResult} ${_matchedVars} PARENT_SCOPE)
endfunction()

function (check_testcode_compiles testcode libraries _successful)
	set (save_required_libraries ${CMAKE_REQUIRED_LIBRARIES})
	set (CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES} ${libraries}")

	check_cxx_source_compiles("${testcode}" ${_successful})
	set (${_successful} ${${_successful}} PARENT_SCOPE)
	set (CMAKE_REQUIRED_LIBRARIES ${save_required_libraries})
endfunction()

function (test_requires_clock_gettime _enable _linklib)
	# This function tests if clock_gettime can be used
	# - at all
	# - with or without librt

	# Result will be:
	# - CLOCK_MONOTONIC is available, link with librt:
	#   _enable = ON; _linklib = "-lrt".
	# - CLOCK_MONOTONIC is available, link without librt:
	#   _enable = ON; _linklib = "".
	# - CLOCK_MONOTONIC is not available:
	#   _enable = OFF; _linklib = "-".

	set (code "
		#include <time.h>
		int main() {
		  timespec res\;
		  int result = clock_gettime(CLOCK_MONOTONIC, &res)\;
		  return result == 0\;
		}
	")

	check_testcode_compiles(${code} "" HAVE_CLOCK_GETTIME_IN)
	if (HAVE_CLOCK_GETTIME_IN)
		message(STATUS "CLOCK_MONOTONIC: available, no extra libs needed")
		set (${_enable}  ON PARENT_SCOPE)
		set (${_linklib} "" PARENT_SCOPE)
		return()
	endif()

	check_testcode_compiles(${code} "rt" HAVE_CLOCK_GETTIME_LIBRT)
	if (HAVE_CLOCK_GETTIME_LIBRT)
		message(STATUS "CLOCK_MONOTONIC: available, requires -lrt")
		set (${_enable}  ON PARENT_SCOPE)
		set (${_linklib} "-lrt" PARENT_SCOPE)
		return()
	endif()

	set (${_enable}  OFF PARENT_SCOPE)
	set (${_linklib} "-" PARENT_SCOPE)
	message(STATUS "CLOCK_MONOTONIC: not available on this system")
endfunction()

function (parse_compiler_type wct _type _suffix)
	if (wct STREQUAL "")
		set(${_type} "" PARENT_SCOPE)
		set(${_suffix} "" PARENT_SCOPE)
	else()
		string(REPLACE "-" ";" OUTLIST ${wct})
		list(LENGTH OUTLIST OUTLEN)
		list(GET OUTLIST 0 ITEM)
		set(${_type} ${ITEM} PARENT_SCOPE)
		if (OUTLEN LESS 2)
			set(_suffix "" PARENT_SCOPE)
		else()
			list(GET OUTLIST 1 ITEM)
			set(${_suffix} "-${ITEM}" PARENT_SCOPE)
		endif()
	endif()
endfunction()
