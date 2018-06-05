/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#include "hmdfasciiparser.h"
#include "boost/config/warning_disable.hpp"
#include "boost/fusion/include/adapt_struct.hpp"
#include "boost/fusion/include/io.hpp"
#include "boost/spirit/include/phoenix_core.hpp"
#include "boost/spirit/include/phoenix_object.hpp"
#include "boost/spirit/include/phoenix_operator.hpp"
#include "boost/spirit/include/qi.hpp"

//--BOOST SPIRIT PARSER--//

using boost::spirit::ascii::space;

namespace parse {

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

struct hmdfAsciiwSec {
  int yr;
  int mo;
  int da;
  int hr;
  int min;
  int sec;
  double val;
};

struct hmdfAsciiwoSec {
  int yr;
  int mo;
  int da;
  int hr;
  int min;
  double val;
};
}  // namespace parse

BOOST_FUSION_ADAPT_STRUCT(
    parse::hmdfAsciiwSec,
    (int, yr)(int, mo)(int, da)(int, hr)(int, min)(int, sec)(double, val))

BOOST_FUSION_ADAPT_STRUCT(parse::hmdfAsciiwoSec,
                          (int, yr)(int, mo)(int, da)(int, hr)(int, min)(double,
                                                                         val))
namespace parse {
template <typename Iterator>
struct hmdfAsciiwSec_parser
    : qi::grammar<Iterator, hmdfAsciiwSec(), ascii::space_type> {
  hmdfAsciiwSec_parser() : hmdfAsciiwSec_parser::base_type(start) {
    using qi::double_;
    using qi::int_;
    start %= int_ >> int_ >> int_ >> int_ >> int_ >> int_ >> double_;
  }
  qi::rule<Iterator, hmdfAsciiwSec(), ascii::space_type> start;
};

template <typename Iterator>
struct hmdfAsciiwoSec_parser
    : qi::grammar<Iterator, hmdfAsciiwoSec(), ascii::space_type> {
  hmdfAsciiwoSec_parser() : hmdfAsciiwoSec_parser::base_type(start) {
    using qi::double_;
    using qi::int_;
    start %= int_ >> int_ >> int_ >> int_ >> int_ >> double_;
  }
  qi::rule<Iterator, hmdfAsciiwoSec(), ascii::space_type> start;
};
}  // namespace parse

typedef std::string::const_iterator iterator_type;
typedef parse::hmdfAsciiwSec_parser<iterator_type> hmdfwsec_parser;
typedef parse::hmdfAsciiwoSec_parser<iterator_type> hmdfwosec_parser;

//--END BOOST SPIRIT PARSER--//

bool HmdfAsciiParser::splitStringHmdfFormat(std::string &data, int &yr, int &month,
                                            int &day, int &hr, int &min,
                                            int &sec, double &value) {
  using namespace parse;
  hmdfwsec_parser p1;
  hmdfwosec_parser p2;
  hmdfAsciiwSec r1;
  hmdfAsciiwoSec r2;

  std::string::const_iterator iter = data.begin();
  std::string::const_iterator end = data.end();

  bool r = phrase_parse(iter, end, p1, space, r1);
  if (r) {
    yr = r1.yr;
    month = r1.mo;
    day = r1.da;
    hr = r1.hr;
    min = r1.min;
    sec = r1.sec;
    value = r1.val;
    return true;
  } else {
    bool r = phrase_parse(iter, end, p2, space, r2);
    if (r) {
      yr = r2.yr;
      month = r2.mo;
      day = r2.da;
      hr = r2.hr;
      min = r2.min;
      sec = 0;
      value = r2.val;
      return true;
    } else {
      return false;
    }
  }
}
