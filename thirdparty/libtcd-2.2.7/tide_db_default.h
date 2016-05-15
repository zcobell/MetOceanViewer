/* $Id: tide_db_default.h 1092 2006-11-16 03:02:42Z flaterco $ */

#include "tcd.h"



/*****************************************************************************\

                            DISTRIBUTION STATEMENT

    This source file is unclassified, distribution unlimited, public
    domain.  It is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

\*****************************************************************************/



#define DEFAULT_HEADER_SIZE                         4096
#define DEFAULT_NUMBER_OF_RECORDS                   0
#define DEFAULT_LEVEL_UNIT_TYPES                    5
#define DEFAULT_DIR_UNIT_TYPES                      3
#define DEFAULT_RESTRICTION_TYPES                   2
#define DEFAULT_RESTRICTION_BITS                    4
#define DEFAULT_TZFILES                             406
#define DEFAULT_TZFILE_BITS                         10
#define DEFAULT_COUNTRIES                           240
#define DEFAULT_COUNTRY_BITS                        9
#define DEFAULT_DATUM_TYPES                         61
#define DEFAULT_DATUM_BITS                          7
#define DEFAULT_LEGALESES                           1
#define DEFAULT_LEGALESE_BITS                       4
#define DEFAULT_SPEED_SCALE                         10000000
#define DEFAULT_EQUILIBRIUM_SCALE                   100
#define DEFAULT_NODE_SCALE                          10000
#define DEFAULT_AMPLITUDE_BITS                      19
#define DEFAULT_AMPLITUDE_SCALE                     10000
#define DEFAULT_EPOCH_BITS                          16
#define DEFAULT_EPOCH_SCALE                         100
#define DEFAULT_RECORD_TYPE_BITS                    4
#define DEFAULT_LATITUDE_BITS                       25
#define DEFAULT_LATITUDE_SCALE                      100000
#define DEFAULT_LONGITUDE_BITS                      26
#define DEFAULT_LONGITUDE_SCALE                     100000
#define DEFAULT_RECORD_SIZE_BITS                    16
#define DEFAULT_STATION_BITS                        18
#define DEFAULT_DATUM_OFFSET_BITS                   28
#define DEFAULT_DATUM_OFFSET_SCALE                  10000
#define DEFAULT_DATE_BITS                           27
#define DEFAULT_MONTHS_ON_STATION_BITS              10
#define DEFAULT_CONFIDENCE_VALUE_BITS               4
#define DEFAULT_NUMBER_OF_CONSTITUENTS_BITS         8
#define DEFAULT_TIME_BITS                           13
#define DEFAULT_LEVEL_ADD_BITS                      17
#define DEFAULT_LEVEL_ADD_SCALE                     1000
#define DEFAULT_LEVEL_MULTIPLY_BITS                 16
#define DEFAULT_LEVEL_MULTIPLY_SCALE                1000
#define DEFAULT_DIRECTION_BITS                      9
#define DEFAULT_CONSTITUENT_SIZE                    10
#define DEFAULT_LEVEL_UNIT_SIZE                     15
#define DEFAULT_DIR_UNIT_SIZE                       15
#define DEFAULT_RESTRICTION_SIZE                    30
#define DEFAULT_DATUM_SIZE                          70
#define DEFAULT_LEGALESE_SIZE                       70
#define DEFAULT_TZFILE_SIZE                         30
#define DEFAULT_COUNTRY_SIZE                        50


/*  Stuff for inferring constituents (NAVO short duration tide stations).  */

#define INFERRED_SEMI_DIURNAL_COUNT                 10
#define INFERRED_DIURNAL_COUNT                      10

NV_CHAR *inferred_semi_diurnal[INFERRED_SEMI_DIURNAL_COUNT] = {
  "N2", "NU2", "MU2", "2N2", "LDA2", "T2", "R2", "L2", "K2", "KJ2"};
NV_CHAR *inferred_diurnal[INFERRED_DIURNAL_COUNT] = {
  "OO1", "M1", "J1", "RHO1", "Q1", "2Q1", "P1", "PI1", "PHI1", "PSI1"};
NV_FLOAT32 semi_diurnal_coeff[INFERRED_SEMI_DIURNAL_COUNT] = {
  .1759, .0341, .0219, .0235, .0066, .0248, .0035, .0251, .1151, .0064};
NV_FLOAT32 diurnal_coeff[INFERRED_DIURNAL_COUNT] = {
  .0163, .0209, .0297, .0142, .0730, .0097, .1755, .0103, .0076, .0042};

/*  These represent M2 and O1.  */

NV_FLOAT32 coeff[2] = {.9085, .3771};


/* The following lookup tables are only used for initialization
   purposes and in the pull-down menus in TideEditor.  It should be
   possible to change them without breaking existing TCD files.  TCD
   files embed their own lookup tables.
*/


/*  Level unit names  */

NV_CHAR level_unit[DEFAULT_LEVEL_UNIT_TYPES][DEFAULT_LEVEL_UNIT_SIZE] = {
  "Unknown", "feet", "meters", "knots", "knots^2"};


/*  Direction unit names  */

NV_CHAR dir_unit[DEFAULT_DIR_UNIT_TYPES][DEFAULT_DIR_UNIT_SIZE] = {
  "Unknown", "degrees true", "degrees"};


/*  Restriction types  */

NV_CHAR restriction[DEFAULT_RESTRICTION_TYPES][DEFAULT_RESTRICTION_SIZE] = {
  "Public Domain", "DoD/DoD Contractors Only"};


/*  Legaleses  */

NV_CHAR legalese[DEFAULT_LEGALESES][DEFAULT_LEGALESE_SIZE] = {
  "NULL"};


/*  # Datum names  */

NV_CHAR datum[DEFAULT_DATUM_TYPES][DEFAULT_DATUM_SIZE] = {
  "Unknown", "Mean Sea Level", "Mean Low Water", "Mean Lower Low Water",
  "Mean High Water", "Mean Higher High Water", "Mean Lower High Water",
  "Mean Higher Low Water", "Mean Low Water Springs",
  "Mean Lower Low Water Springs", "Mean Low Water Neaps",
  "Mean High Water Neaps", "Mean High Water Springs",
  "Mean Higher High Water Springs", "Indian Spring Low Water",
  "Equatorial Spring Low Water", "Lowest Normal Low Water", "Lowest Low Water",
  "Lowest Possible Low Water", "Lowest Astronomical Tide",
  "International Great Lakes Datum(1955)", "Lower Low Water, Large Tide",
  "Lowest Normal Tide", "Higher High Water, Large Tide", "Mean Water Level",
  "Higher High Water, Mean Tide", "Lower Low Water, Mean Tide",
  "Mean Tide Level", "World Geodetic System (1984)",
  "National Geodetic Vertical Datum", "Gulf Coast Low Water Datum",
  "Approximate Level of Mean Sea Level",
  "Approximate Level of Mean Low Water",
  "Approximate Level of Mean Lower Low Water",
  "Approximate Level of Mean High Water",
  "Approximate Level of Mean Higher High Water",
  "Approximate Level of Mean Lower High Water",
  "Approximate Level of Mean Higher Low Water",
  "Approximate Level of Mean Low Water Springs",
  "Approximate Level of Mean Lower Low Water Springs",
  "Approximate Level of Mean Low Water Neaps",
  "Approximate Level of Mean High Water Neaps",
  "Approximate Level of Mean High Water Springs",
  "Approximate Level of Mean Higher High Water Springs",
  "Approximate Level of Indian Spring Low Water",
  "Approximate Level of Equatorial Spring Low Water",
  "Approximate Level of Lowest Normal Low Water",
  "Approximate Level of Lowest Low Water",
  "Approximate Level of Lowest Possible Low Water",
  "Approximate Level of Lowest Astronomical Tide",
  "Approximate Level of International Great Lakes Datum (1955)",
  "Approximate Level of Lower Low Water, Large Tide",
  "Approximate Level of Lowest Normal Tide",
  "Approximate Level of Higher High Water, Large Tide",
  "Approximate Level of Mean Water Level",
  "Approximate Level of Higher High Water, Mean Tide",
  "Approximate Level of Lower Low Water, Mean Tide",
  "Approximate Level of Mean Tide Level",
  "Approximate Level of World Geodetic System (1984)",
  "Approximate Level of National Geodetic Vertical Datum",
  "Approximate Level of Gulf Coast Low Water Datum"};


/*  # Country names from ISO 3166-1:1999 2-character country code list  */

NV_CHAR country[DEFAULT_COUNTRIES][DEFAULT_COUNTRY_SIZE] = {"Unknown",
  "Afghanistan", "Albania", "Algeria", "Andorra", "Angola", "Anguilla",
  "Antarctica", "Antigua & Barbuda", "Argentina", "Armenia", "Aruba",
  "Australia", "Austria", "Azerbaijan", "Bahamas", "Bahrain", "Bangladesh",
  "Barbados", "Belarus", "Belgium", "Belize", "Benin", "Bermuda", "Bhutan",
  "Bolivia", "Bosnia & Herzegovina", "Botswana", "Bouvet Island", "Brazil",
  "Britain (UK)", "British Indian Ocean Territory", "Brunei", "Bulgaria",
  "Burkina Faso", "Burundi", "Cambodia", "Cameroon", "Canada", "Cape Verde",
  "Cayman Islands", "Central African Rep.", "Chad", "Chile", "China",
  "Christmas Island", "Cocos (Keeling) Islands", "Colombia", "Comoros",
  "Congo (Dem. Rep.)", "Congo (Rep.)", "Cook Islands", "Costa Rica",
  "Cote d'Ivoire", "Croatia", "Cuba", "Cyprus", "Czech Republic", "Denmark",
  "Djibouti", "Dominica", "Dominican Republic", "East Timor", "Ecuador",
  "Egypt", "El Salvador", "Equatorial Guinea", "Eritrea", "Estonia",
  "Ethiopia", "Faeroe Islands", "Falkland Islands", "Fiji", "Finland",
  "France", "French Guiana", "French Polynesia",
  "French Southern & Antarctic Lands", "Gabon", "Gambia", "Georgia", "Germany",
  "Ghana", "Gibraltar", "Greece", "Greenland", "Grenada", "Guadeloupe", "Guam",
  "Guatemala", "Guinea", "Guinea-Bissau", "Guyana", "Haiti",
  "Heard Island & McDonald Islands", "Honduras", "Hong Kong", "Hungary",
  "Iceland", "India", "Indonesia", "Iran", "Iraq", "Ireland", "Israel",
  "Italy", "Jamaica", "Japan", "Jordan", "Kazakhstan", "Kenya", "Kiribati",
  "Korea (North)", "Korea (South)", "Kuwait", "Kyrgyzstan", "Laos", "Latvia",
  "Lebanon", "Lesotho", "Liberia", "Libya", "Liechtenstein", "Lithuania",
  "Luxembourg", "Macau", "Macedonia", "Madagascar", "Malawi", "Malaysia",
  "Maldives", "Mali", "Malta", "Marshall Islands", "Martinique", "Mauritania",
  "Mauritius", "Mayotte", "Mexico", "Micronesia", "Moldova", "Monaco",
  "Mongolia", "Montserrat", "Morocco", "Mozambique", "Myanmar (Burma)",
  "Namibia", "Nauru", "Nepal", "Netherlands", "Netherlands Antilles",
  "New Caledonia", "New Zealand", "Nicaragua", "Niger", "Nigeria", "Niue",
  "Norfolk Island", "Northern Mariana Islands", "Norway", "Oman", "Pakistan",
  "Palau", "Palestine", "Panama", "Papua New Guinea", "Paraguay", "Peru",
  "Philippines", "Pitcairn", "Poland", "Portugal", "Puerto Rico", "Qatar",
  "Reunion", "Romania", "Russia", "Rwanda", "Samoa (American)",
  "Samoa (Western)", "San Marino", "Sao Tome & Principe", "Saudi Arabia",
  "Senegal", "Seychelles", "Sierra Leone", "Singapore", "Slovakia", "Slovenia",
  "Solomon Islands", "Somalia", "South Africa",
  "South Georgia & the South Sandwich Islands", "Spain", "Sri Lanka",
  "St Helena", "St Kitts & Nevis", "St Lucia", "St Pierre & Miquelon",
  "St Vincent", "Sudan", "Suriname", "Svalbard & Jan Mayen", "Swaziland",
  "Sweden", "Switzerland", "Syria", "Taiwan", "Tajikistan", "Tanzania",
  "Thailand", "Togo", "Tokelau", "Tonga", "Trinidad & Tobago", "Tunisia",
  "Turkey", "Turkmenistan", "Turks & Caicos Is", "Tuvalu", "Uganda", "Ukraine",
  "United Arab Emirates", "United States", "Uruguay",
  "US minor outlying islands", "Uzbekistan", "Vanuatu", "Vatican City",
  "Venezuela", "Vietnam", "Virgin Islands (UK)", "Virgin Islands (US)",
  "Wallis & Futuna", "Western Sahara", "Yemen", "Yugoslavia", "Zambia",
  "Zimbabwe"};


/*  # Time zones extracted from tzdata2002? .  */

NV_CHAR tzfile[DEFAULT_TZFILES][DEFAULT_TZFILE_SIZE] = {"Unknown",
  ":Africa/Abidjan", ":Africa/Accra", ":Africa/Addis_Ababa",
  ":Africa/Algiers", ":Africa/Asmera", ":Africa/Bamako",
  ":Africa/Bangui", ":Africa/Banjul", ":Africa/Bissau",
  ":Africa/Blantyre", ":Africa/Brazzaville", ":Africa/Bujumbura",
  ":Africa/Cairo", ":Africa/Casablanca", ":Africa/Ceuta",
  ":Africa/Conakry", ":Africa/Dakar", ":Africa/Dar_es_Salaam",
  ":Africa/Djibouti", ":Africa/Douala", ":Africa/El_Aaiun",
  ":Africa/Freetown", ":Africa/Gaborone", ":Africa/Harare",
  ":Africa/Johannesburg", ":Africa/Kampala", ":Africa/Khartoum",
  ":Africa/Kigali", ":Africa/Kinshasa", ":Africa/Lagos",
  ":Africa/Libreville", ":Africa/Lome", ":Africa/Luanda",
  ":Africa/Lubumbashi", ":Africa/Lusaka", ":Africa/Malabo",
  ":Africa/Maputo", ":Africa/Maseru", ":Africa/Mbabane",
  ":Africa/Mogadishu", ":Africa/Monrovia", ":Africa/Nairobi",
  ":Africa/Ndjamena", ":Africa/Niamey", ":Africa/Nouakchott",
  ":Africa/Ouagadougou", ":Africa/Porto-Novo", ":Africa/Sao_Tome",
  ":Africa/Timbuktu", ":Africa/Tripoli", ":Africa/Tunis",
  ":Africa/Windhoek", ":America/Adak", ":America/Anchorage",
  ":America/Anguilla", ":America/Antigua", ":America/Araguaina",
  ":America/Aruba", ":America/Asuncion", ":America/Atka",
  ":America/Barbados", ":America/Belem", ":America/Belize",
  ":America/Boa_Vista", ":America/Bogota", ":America/Boise",
  ":America/Buenos_Aires", ":America/Cambridge_Bay",
  ":America/Cancun", ":America/Caracas", ":America/Catamarca",
  ":America/Cayenne", ":America/Cayman", ":America/Chicago",
  ":America/Chihuahua", ":America/Cordoba", ":America/Costa_Rica",
  ":America/Cuiaba", ":America/Curacao", ":America/Danmarkshavn",
  ":America/Dawson", ":America/Dawson_Creek", ":America/Denver",
  ":America/Detroit", ":America/Dominica", ":America/Edmonton",
  ":America/Eirunepe", ":America/El_Salvador", ":America/Ensenada",
  ":America/Fortaleza", ":America/Glace_Bay", ":America/Godthab",
  ":America/Goose_Bay", ":America/Grand_Turk", ":America/Grenada",
  ":America/Guadeloupe", ":America/Guatemala", ":America/Guayaquil",
  ":America/Guyana", ":America/Halifax", ":America/Havana",
  ":America/Hermosillo", ":America/Indiana/Knox",
  ":America/Indiana/Marengo", ":America/Indianapolis",
  ":America/Indiana/Vevay", ":America/Inuvik", ":America/Iqaluit",
  ":America/Jamaica", ":America/Jujuy", ":America/Juneau",
  ":America/Kentucky/Monticello", ":America/La_Paz", ":America/Lima",
  ":America/Los_Angeles", ":America/Louisville", ":America/Maceio",
  ":America/Managua", ":America/Manaus", ":America/Martinique",
  ":America/Mazatlan", ":America/Mendoza", ":America/Menominee",
  ":America/Merida", ":America/Mexico_City", ":America/Miquelon",
  ":America/Monterrey", ":America/Montevideo", ":America/Montreal",
  ":America/Montserrat", ":America/Nassau", ":America/New_York",
  ":America/Nipigon", ":America/Nome", ":America/Noronha",
  ":America/North_Dakota/Center", ":America/Panama",
  ":America/Pangnirtung", ":America/Paramaribo", ":America/Phoenix",
  ":America/Port-au-Prince", ":America/Port_of_Spain",
  ":America/Porto_Velho", ":America/Puerto_Rico",
  ":America/Rainy_River", ":America/Rankin_Inlet", ":America/Recife",
  ":America/Regina", ":America/Rio_Branco", ":America/Santiago",
  ":America/Santo_Domingo", ":America/Sao_Paulo",
  ":America/Scoresbysund", ":America/Shiprock", ":America/St_Johns",
  ":America/St_Kitts", ":America/St_Lucia", ":America/St_Thomas",
  ":America/St_Vincent", ":America/Swift_Current",
  ":America/Tegucigalpa", ":America/Thule", ":America/Thunder_Bay",
  ":America/Tijuana", ":America/Tortola", ":America/Vancouver",
  ":America/Whitehorse", ":America/Winnipeg", ":America/Yakutat",
  ":America/Yellowknife", ":Antarctica/Casey", ":Antarctica/Davis",
  ":Antarctica/DumontDUrville", ":Antarctica/Mawson",
  ":Antarctica/McMurdo", ":Antarctica/Palmer",
  ":Antarctica/South_Pole", ":Antarctica/Syowa", ":Antarctica/Vostok",
  ":Arctic/Longyearbyen", ":Asia/Aden", ":Asia/Almaty", ":Asia/Amman",
  ":Asia/Anadyr", ":Asia/Aqtau", ":Asia/Aqtobe", ":Asia/Ashgabat",
  ":Asia/Baghdad", ":Asia/Bahrain", ":Asia/Baku", ":Asia/Bangkok",
  ":Asia/Beirut", ":Asia/Bishkek", ":Asia/Brunei", ":Asia/Calcutta",
  ":Asia/Choibalsan", ":Asia/Chongqing", ":Asia/Colombo",
  ":Asia/Damascus", ":Asia/Dhaka", ":Asia/Dili", ":Asia/Dubai",
  ":Asia/Dushanbe", ":Asia/Gaza", ":Asia/Harbin", ":Asia/Hong_Kong",
  ":Asia/Hovd", ":Asia/Irkutsk", ":Asia/Jakarta", ":Asia/Jayapura",
  ":Asia/Jerusalem", ":Asia/Kabul", ":Asia/Kamchatka",
  ":Asia/Karachi", ":Asia/Kashgar", ":Asia/Katmandu",
  ":Asia/Krasnoyarsk", ":Asia/Kuala_Lumpur", ":Asia/Kuching",
  ":Asia/Kuwait", ":Asia/Macau", ":Asia/Magadan", ":Asia/Makassar",
  ":Asia/Manila", ":Asia/Muscat", ":Asia/Nicosia",
  ":Asia/Novosibirsk", ":Asia/Omsk", ":Asia/Oral", ":Asia/Phnom_Penh",
  ":Asia/Pontianak", ":Asia/Pyongyang", ":Asia/Qatar",
  ":Asia/Qyzylorda", ":Asia/Rangoon", ":Asia/Riyadh", ":Asia/Saigon",
  ":Asia/Sakhalin", ":Asia/Samarkand", ":Asia/Seoul",
  ":Asia/Shanghai", ":Asia/Singapore", ":Asia/Taipei",
  ":Asia/Tashkent", ":Asia/Tbilisi", ":Asia/Tehran", ":Asia/Thimphu",
  ":Asia/Tokyo", ":Asia/Ulaanbaatar", ":Asia/Urumqi",
  ":Asia/Vientiane", ":Asia/Vladivostok", ":Asia/Yakutsk",
  ":Asia/Yekaterinburg", ":Asia/Yerevan", ":Atlantic/Azores",
  ":Atlantic/Bermuda", ":Atlantic/Canary", ":Atlantic/Cape_Verde",
  ":Atlantic/Faeroe", ":Atlantic/Jan_Mayen", ":Atlantic/Madeira",
  ":Atlantic/Reykjavik", ":Atlantic/South_Georgia",
  ":Atlantic/Stanley", ":Atlantic/St_Helena", ":Australia/Adelaide",
  ":Australia/Brisbane", ":Australia/Broken_Hill",
  ":Australia/Darwin", ":Australia/Hobart", ":Australia/Lindeman",
  ":Australia/Lord_Howe", ":Australia/Melbourne", ":Australia/Perth",
  ":Australia/Sydney", ":Etc/GMT", ":Etc/GMT-1", ":Etc/GMT+1",
  ":Etc/GMT-10", ":Etc/GMT+10", ":Etc/GMT-11", ":Etc/GMT+11",
  ":Etc/GMT-12", ":Etc/GMT+12", ":Etc/GMT-13", ":Etc/GMT-14",
  ":Etc/GMT-2", ":Etc/GMT+2", ":Etc/GMT-3", ":Etc/GMT+3",
  ":Etc/GMT-4", ":Etc/GMT+4", ":Etc/GMT-5", ":Etc/GMT+5",
  ":Etc/GMT-6", ":Etc/GMT+6", ":Etc/GMT-7", ":Etc/GMT+7",
  ":Etc/GMT-8", ":Etc/GMT+8", ":Etc/GMT-9", ":Etc/GMT+9", ":Etc/UCT",
  ":Etc/UTC", ":Europe/Amsterdam", ":Europe/Andorra",
  ":Europe/Athens", ":Europe/Belfast", ":Europe/Belgrade",
  ":Europe/Berlin", ":Europe/Bratislava", ":Europe/Brussels",
  ":Europe/Bucharest", ":Europe/Budapest", ":Europe/Chisinau",
  ":Europe/Copenhagen", ":Europe/Dublin", ":Europe/Gibraltar",
  ":Europe/Helsinki", ":Europe/Istanbul", ":Europe/Kaliningrad",
  ":Europe/Kiev", ":Europe/Lisbon", ":Europe/Ljubljana",
  ":Europe/London", ":Europe/Luxembourg", ":Europe/Madrid",
  ":Europe/Malta", ":Europe/Minsk", ":Europe/Monaco",
  ":Europe/Moscow", ":Europe/Oslo", ":Europe/Paris", ":Europe/Prague",
  ":Europe/Riga", ":Europe/Rome", ":Europe/Samara",
  ":Europe/San_Marino", ":Europe/Sarajevo", ":Europe/Simferopol",
  ":Europe/Skopje", ":Europe/Sofia", ":Europe/Stockholm",
  ":Europe/Tallinn", ":Europe/Tirane", ":Europe/Uzhgorod",
  ":Europe/Vaduz", ":Europe/Vatican", ":Europe/Vienna",
  ":Europe/Vilnius", ":Europe/Warsaw", ":Europe/Zagreb",
  ":Europe/Zaporozhye", ":Europe/Zurich", ":Indian/Antananarivo",
  ":Indian/Chagos", ":Indian/Christmas", ":Indian/Cocos",
  ":Indian/Comoro", ":Indian/Kerguelen", ":Indian/Mahe",
  ":Indian/Maldives", ":Indian/Mauritius", ":Indian/Mayotte",
  ":Indian/Reunion", ":Pacific/Apia", ":Pacific/Auckland",
  ":Pacific/Chatham", ":Pacific/Easter", ":Pacific/Efate",
  ":Pacific/Enderbury", ":Pacific/Fakaofo", ":Pacific/Fiji",
  ":Pacific/Funafuti", ":Pacific/Galapagos", ":Pacific/Gambier",
  ":Pacific/Guadalcanal", ":Pacific/Guam", ":Pacific/Honolulu",
  ":Pacific/Johnston", ":Pacific/Kiritimati", ":Pacific/Kosrae",
  ":Pacific/Kwajalein", ":Pacific/Majuro", ":Pacific/Marquesas",
  ":Pacific/Midway", ":Pacific/Nauru", ":Pacific/Niue",
  ":Pacific/Norfolk", ":Pacific/Noumea", ":Pacific/Pago_Pago",
  ":Pacific/Palau", ":Pacific/Pitcairn", ":Pacific/Ponape",
  ":Pacific/Port_Moresby", ":Pacific/Rarotonga", ":Pacific/Saipan",
  ":Pacific/Tahiti", ":Pacific/Tarawa", ":Pacific/Tongatapu",
  ":Pacific/Truk", ":Pacific/Wake", ":Pacific/Wallis",
  ":Pacific/Yap"};
