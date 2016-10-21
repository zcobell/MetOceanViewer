// $Id: xxLocationList.hh 4256 2012-02-05 18:35:11Z flaterco $

/*  xxLocationList   Scrolling location chooser.

    Copyright (C) 1998  David Flater.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

class xxMultiChoice;

class xxLocationList: public xxWindow,
		      public xxMouseWheelViewport,
		      public xxReconfigurable {
public:

  // xxLocationList takes ownership of the stationIndex.
  xxLocationList (const xxWidget &parent,
		  StationIndex *stationIndex,
		  xxWindow *globeOrMap);
  ~xxLocationList ();

  void continuationButton (Widget button);
  void pickStation (const XawListReturnStruct *xrs);
  void reconfigure();
  void changeList (StationIndex *stationIndex);
  xxWindow * const dismiss();

  void globalRedraw();                // See xxRedrawable.

protected:
  xxWindow *_globeOrMap;
  std::auto_ptr<StationIndex> _stationIndex;
  std::auto_ptr<xxWidget> viewport, list, label, helpButton;
  std::auto_ptr<xxMultiChoice> sortChoice;

  unsigned long startAt;
  StationIndex::SortKey _sortKey;
  unsigned maxLocListLength;

  // Neither the continuation buttons nor the string list can
  // productively be managed with auto_ptr and/or a SafeVector.
  SafeVector<xxWidget*> continuationButtons;
  char **stringList;

  void refreshList();
  void freeStringList();
  void doContinuationButtons();
  void listChanged();
};

// Cleanup2006 Done
