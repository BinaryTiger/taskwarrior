////////////////////////////////////////////////////////////////////////////////
// task - a command line task list manager.
//
// Copyright 2006 - 2009, Paul Beckingham.
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the
//
//     Free Software Foundation, Inc.,
//     51 Franklin Street, Fifth Floor,
//     Boston, MA
//     02110-1301
//     USA
//
////////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <stdlib.h>
#include <string>
#include <vector>
#include <map>

#include "Context.h"
#include "Date.h"
#include "Duration.h"
#include "T.h"
#include "text.h"
#include "util.h"

extern Context context;

////////////////////////////////////////////////////////////////////////////////
// NOTE: These are static arrays only because there is no initializer list for
//       std::vector.
static const char* colors[] =
{
  "bold",
  "underline",
  "bold_underline",

  "black",
  "red",
  "green",
  "yellow",
  "blue",
  "magenta",
  "cyan",
  "white",

  "bold_black",
  "bold_red",
  "bold_green",
  "bold_yellow",
  "bold_blue",
  "bold_magenta",
  "bold_cyan",
  "bold_white",

  "underline_black",
  "underline_red",
  "underline_green",
  "underline_yellow",
  "underline_blue",
  "underline_magenta",
  "underline_cyan",
  "underline_white",

  "bold_underline_black",
  "bold_underline_red",
  "bold_underline_green",
  "bold_underline_yellow",
  "bold_underline_blue",
  "bold_underline_magenta",
  "bold_underline_cyan",
  "bold_underline_white",

  "on_black",
  "on_red",
  "on_green",
  "on_yellow",
  "on_blue",
  "on_magenta",
  "on_cyan",
  "on_white",

  "on_bright_black",
  "on_bright_red",
  "on_bright_green",
  "on_bright_yellow",
  "on_bright_blue",
  "on_bright_magenta",
  "on_bright_cyan",
  "on_bright_white",
  "",
};

static const char* attributes[] =
{
  "project",
  "priority",
  "fg",
  "bg",
  "due",
  "entry",
  "start",
  "end",
  "recur",
  "until",
  "mask",
  "imask",
  "",
};

// Alphabetical please.
static const char* commands[] =
{
  "active",
  "add",
  "append",
  "annotate",
  "calendar",
  "colors",
  "completed",
  "delete",
  "done",
  "duplicate",
  "edit",
  "export",
  "help",
  "history",
  "ghistory",
  "import",
  "info",
  "next",
  "overdue",
  "projects",
  "start",
  "stats",
  "stop",
  "summary",
  "tags",
  "timesheet",
  "undelete",
  "undo",
  "version",
  "",
};

static std::vector <std::string> customReports;

////////////////////////////////////////////////////////////////////////////////
void guess (
  const std::string& type,
  const char** list,
  std::string& candidate)
{
  std::vector <std::string> options;
  for (int i = 0; list[i][0]; ++i)
    options.push_back (list[i]);

  guess (type, options, candidate);
}

////////////////////////////////////////////////////////////////////////////////
static bool isCommand (const std::string& candidate)
{
  std::vector <std::string> options;
  for (int i = 0; commands[i][0]; ++i)
    options.push_back (commands[i]);

  std::vector <std::string> matches;
  autoComplete (candidate, options, matches);
  if (0 == matches.size ())
  {
    autoComplete (candidate, customReports, matches);
    if (0 == matches.size ())
      return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool validDate (std::string& date)
{
  Date test (date, context.config.get ("dateformat", "m/d/Y"));

  char epoch[16];
  sprintf (epoch, "%d", (int) test.toEpoch ());
  date = epoch;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
bool validPriority (const std::string& input)
{
  if (input != "H" &&
      input != "M" &&
      input != "L" &&
      input != "")
    throw std::string ("\"") +
          input              +
          "\" is not a valid priority.  Use H, M, L or leave blank.";

  return true;
}

////////////////////////////////////////////////////////////////////////////////
static bool validAttribute (
  std::string& name,
  std::string& value)
{
  guess ("attribute", attributes, name);
  if (name != "")
  {
    if ((name == "fg" || name == "bg") && value != "")
      guess ("color", colors, value);

    else if (name == "due" && value != "")
      validDate (value);

    else if (name == "until" && value != "")
      validDate (value);

    else if (name == "priority")
    {
      value = upperCase (value);
      return validPriority (value);
    }

    // Some attributes are intended to be private.
    else if (name == "entry" ||
             name == "start" ||
             name == "end"   ||
             name == "mask"  ||
             name == "imask")
      throw std::string ("\"") +
            name               +
            "\" is not an attribute you may modify directly.";

    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
static bool validId (const std::string& input)
{
  if (input.length () == 0)
    return false;

  for (size_t i = 0; i < input.length (); ++i)
    if (!::isdigit (input[i]))
      return false;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
// 1,2-4,6
static bool validSequence (
  const std::string& input,
  std::vector <int>& ids)
{
  std::vector <std::string> ranges;
  split (ranges, input, ',');

  std::vector <std::string>::iterator it;
  for (it = ranges.begin (); it != ranges.end (); ++it)
  {
    std::vector <std::string> range;
    split (range, *it, '-');

    switch (range.size ())
    {
    case 1:
      {
        if (! validId (range[0]))
          return false;

        int id = ::atoi (range[0].c_str ());
        ids.push_back (id);
      }
      break;

    case 2:
      {
        if (! validId (range[0]) ||
            ! validId (range[1]))
          return false;

        int low  = ::atoi (range[0].c_str ());
        int high = ::atoi (range[1].c_str ());
        if (low >= high)
          return false;

        for (int i = low; i <= high; ++i)
          ids.push_back (i);
      }
      break;

    default:
      return false;
      break;
    }
  }

  return ids.size () ? true : false;
}

////////////////////////////////////////////////////////////////////////////////
static bool validTag (const std::string& input)
{
  if ((input[0] == '-' || input[0] == '+') &&
       input.length () > 1)
    return true;

  return false;
}

////////////////////////////////////////////////////////////////////////////////
bool validDescription (const std::string& input)
{
  if (input.length ()                        &&
      input.find ("\r") == std::string::npos &&
      input.find ("\f") == std::string::npos &&
      input.find ("\n") == std::string::npos)
    return true;

  return false;
}

////////////////////////////////////////////////////////////////////////////////
bool validCommand (std::string& input)
{
  std::string copy = input;
  guess ("command", commands, copy);
  if (copy == "")
  {
    copy = input;
    guess ("command", customReports, copy);
    if (copy == "")
      return false;
  }

  input = copy;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
static bool validSubstitution (
  std::string& input,
  std::string& from,
  std::string& to,
  bool& global)
{
  size_t first = input.find ('/');
  if (first != std::string::npos)
  {
    size_t second = input.find ('/', first + 1);
    if (second != std::string::npos)
    {
      size_t third = input.find ('/', second + 1);
      if (third != std::string::npos)
      {
        if (first == 0 &&
            first < second &&
            second < third &&
            (third == input.length () - 1 ||
             third == input.length () - 2))
        {
          from = input.substr (first  + 1, second - first  - 1);
          to   = input.substr (second + 1, third  - second - 1);

          global = false;
          if (third == input.length () - 2 &&
              input.find ('g', third + 1) != std::string::npos)
            global = true;

          return true;
        }
      }
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
bool validDuration (std::string& input)
{
  try         { Duration (input); }
  catch (...) { return false;     }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Token        EBNF
// -------      ----------------------------------
// command      first non-id recognized argument
//
// substitution ::= "/" from "/" to "/g"
//                | "/" from "/" to "/" ;
//
// tags         ::= "+" word
//                | "-" word ;
//
// attributes   ::= word ":" value
//                | word ":"
//
// sequence     ::= \d+ "," sequence
//                | \d+ "-" \d+ ;
//
// description  (whatever isn't one of the above)
/*
void parse (
  std::vector <std::string>& args,
  std::string& command,
  T& task)
{
  command = "";

  bool terminated = false;
  bool foundSequence = false;
  bool foundSomethingAfterSequence = false;

  std::string descCandidate = "";
  for (size_t i = 0; i < args.size (); ++i)
  {
    std::string arg (args[i]);

    if (!terminated)
    {
      size_t colon;               // Pointer to colon in argument.
      std::string from;
      std::string to;
      bool global;
      std::vector <int> sequence;

      // The '--' argument shuts off all parsing - everything is an argument.
      if (arg == "--")
        terminated = true;

      // An id is the first argument found that contains all digits.
      else if (lowerCase (command) != "add"  && // "add" doesn't require an ID
          validSequence (arg, sequence) &&
          ! foundSomethingAfterSequence)
      {
        foundSequence = true;
        foreach (id, sequence)
          task.addId (*id);
      }

      // Tags begin with + or - and contain arbitrary text.
      else if (validTag (arg))
      {
        if (foundSequence)
          foundSomethingAfterSequence = true;

        if (arg[0] == '+')
          task.addTag (arg.substr (1, std::string::npos));
        else if (arg[0] == '-')
          task.addRemoveTag (arg.substr (1, std::string::npos));
      }

      // Attributes contain a constant string followed by a colon, followed by a
      // value.
      else if ((colon = arg.find (":")) != std::string::npos)
      {
        if (foundSequence)
          foundSomethingAfterSequence = true;

        std::string name  = lowerCase (arg.substr (0, colon));
        std::string value = arg.substr (colon + 1, std::string::npos);

        if (validAttribute (name, value))
        {
          if (name != "recur" || validDuration (value))
            task.setAttribute (name, value);
        }

        // If it is not a valid attribute, then allow the argument as part of
        // the description.
        else
        {
          if (descCandidate.length ())
            descCandidate += " ";
          descCandidate += arg;
        }
      }

      // Substitution of description text.
      else if (validSubstitution (arg, from, to, global))
      {
        if (foundSequence)
          foundSomethingAfterSequence = true;

        task.setSubstitution (from, to, global);
      }

      // Command.
      else if (command == "")
      {
        if (foundSequence)
          foundSomethingAfterSequence = true;

        std::string l = lowerCase (arg);
        if (isCommand (l) && validCommand (l))
          command = l;
        else
        {
          if (descCandidate.length ())
            descCandidate += " ";
          descCandidate += arg;
        }
      }

      // Anything else is just considered description.
      else
      {
        if (foundSequence)
          foundSomethingAfterSequence = true;

        if (descCandidate.length ())
          descCandidate += " ";
        descCandidate += arg;
      }
    }
    // terminated, therefore everything subsequently is a description.
    else
    {
      if (foundSequence)
        foundSomethingAfterSequence = true;

      if (descCandidate.length ())
        descCandidate += " ";
      descCandidate += arg;
    }
  }

  if (validDescription (descCandidate))
    task.setDescription (descCandidate);
}
*/

////////////////////////////////////////////////////////////////////////////////
void validReportColumns (const std::vector <std::string>& columns)
{
  std::vector <std::string> bad;

  std::vector <std::string>::const_iterator it;
  for (it = columns.begin (); it != columns.end (); ++it)
    if (*it != "id"                   &&
        *it != "uuid"                 &&
        *it != "project"              &&
        *it != "priority"             &&
        *it != "entry"                &&
        *it != "start"                &&
        *it != "due"                  &&
        *it != "age"                  &&
        *it != "age_compact"          &&
        *it != "active"               &&
        *it != "tags"                 &&
        *it != "recur"                &&
        *it != "recurrence_indicator" &&
        *it != "tag_indicator"        &&
        *it != "description_only"     &&
        *it != "description")
      bad.push_back (*it);

  if (bad.size ())
  {
    std::string error;
    join (error, ", ", bad);
    throw std::string ("Unrecognized column name: ") + error;
  }
}

////////////////////////////////////////////////////////////////////////////////
void validSortColumns (
  const std::vector <std::string>& columns,
  const std::vector <std::string>& sortColumns)
{
  std::vector <std::string> bad;
  std::vector <std::string>::const_iterator sc;
  for (sc = sortColumns.begin (); sc != sortColumns.end (); ++sc)
  {
    std::vector <std::string>::const_iterator co;
    for (co = columns.begin (); co != columns.end (); ++co)
      if (sc->substr (0, sc->length () - 1) == *co)
        break;

    if (co == columns.end ())
      bad.push_back (*sc);
  }

  if (bad.size ())
  {
    std::string error;
    join (error, ", ", bad);
    throw std::string ("Sort column is not part of the report: ") + error;
  }
}

////////////////////////////////////////////////////////////////////////////////

