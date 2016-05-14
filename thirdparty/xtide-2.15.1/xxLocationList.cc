// $Id: xxLocationList.cc 5749 2014-10-11 19:42:10Z flaterco $

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

#include "xtide.hh"
#include "xxMultiChoice.hh"
#include "xxReconfigurable.hh"
#include "xxLocationList.hh"


static const unsigned locListViewportInitialHeight (200U);


static void continuationButtonCallback (Widget w,
                                        XtPointer client_data,
                                        XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxLocationList*)client_data)->continuationButton (w);
}


void xxLocationList::continuationButton (Widget button) {
  for (unsigned i=0; i<continuationButtons.size(); ++i)
    if (continuationButtons[i]->widget() == button) {
      freeStringList();
      startAt = i * maxLocListLength;
      stringList = _stationIndex->makeStringList (startAt, maxLocListLength);
      listChanged();
      return;
    }
  assert (false);
}


static void xxLocationListHelpCallback (Widget w unusedParameter,
                                        XtPointer client_data unusedParameter,
                                        XtPointer call_data unusedParameter) {
  Dstr helpstring ("\
XTide Location Chooser -- Location List\n\
\n\
The location list is subordinate to the globe/map window.  When a region\n\
is shown in the globe/map window, all tide stations in that region are\n\
enumerated in the location list.  You can then use the scrollbar to\n\
view the entire list or left-click on locations in the list to obtain\n\
tide predictions.\n\
\n\
If the list is too long to display at one time, it will be broken into\n\
several smaller lists, and the location list window will sprout\n\
buttons to let you switch between them.\n\
\n\
You can change the sort field by pulling down the Sort button.\n\
\n\
The location list is dismissed when the globe/map window is dismissed.");
  (void) xxroot->newHelpBox (helpstring);
}


static void xxLocationListChoiceCallback (Widget w unusedParameter,
                                          XtPointer client_data,
                                          XtPointer call_data) {
  assert (client_data);
  assert (call_data);
  ((xxLocationList*)client_data)->pickStation (
                                              (XawListReturnStruct*)call_data);
}


void xxLocationList::pickStation (const XawListReturnStruct *xrs) {
  xxroot->newGraph (*((*_stationIndex)[xrs->list_index + startAt]));
}


void xxLocationList::listChanged() {
  XawListChange (list->widget(), stringList, 0, 0, 1);
}


xxWindow * const xxLocationList::dismiss() {
  // Redirect the deletion to the globe or map owning this object.
  return _globeOrMap;
}


void xxLocationList::doContinuationButtons() {
  // Get rid of any existing buttons.
  for (SafeVector<xxWidget*>::iterator it = continuationButtons.begin();
       it != continuationButtons.end();
       ++it)
    delete *it;
  continuationButtons.clear();

  assert (maxLocListLength > 1);
  unsigned numContButtons ((unsigned) ceil ((double)_stationIndex->size() /
                                            (double)maxLocListLength));
  if (numContButtons == 1)
    numContButtons = 0;

  if (numContButtons) {
    Widget westWidget = helpButton->widget();
    for (unsigned b=0; b<numContButtons; ++b) {
      Arg buttonArgs[8] =  {
        {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
        {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
	{(char*)XtNfromHoriz, (XtArgVal)westWidget},
	{(char*)XtNfromVert, (XtArgVal)viewport->widget()},
	{(char*)XtNleft, (XtArgVal)XawChainLeft},
	{(char*)XtNright, (XtArgVal)XawChainLeft},
	{(char*)XtNtop, (XtArgVal)XawChainBottom},
	{(char*)XtNbottom, (XtArgVal)XawChainBottom}
      };
      char tbuf[80];
      unsigned long last = (b+1)*maxLocListLength-1;
      if (last >= _stationIndex->size())
        last = _stationIndex->size() - 1;
      switch (_sortKey) {
      case StationIndex::sortByName:
        sprintf (tbuf, "%c-%c", (*_stationIndex)[b*maxLocListLength]->name[0],
                                (*_stationIndex)[last]->name[0]);
        break;
      case StationIndex::sortByLat:
        {
          Dstr lat1, lat2;
          (*_stationIndex)[b*maxLocListLength]->coordinates.printLat (lat1);
          (*_stationIndex)[last]->coordinates.printLat (lat2);
          sprintf (tbuf, "(%s)-(%s)", lat1.aschar(), lat2.aschar());
        }
        break;
      case StationIndex::sortByLng:
        {
          Dstr lng1, lng2;
          (*_stationIndex)[b*maxLocListLength]->coordinates.printLng (lng1);
          (*_stationIndex)[last]->coordinates.printLng (lng2);
          sprintf (tbuf, "(%s)-(%s)", lng1.aschar(), lng2.aschar());
        }
        break;
      default:
        assert (false);
      }
      Widget buttonWidget = xxX::createXtWidget (tbuf,
        commandWidgetClass, container->widget(), buttonArgs, 8);
      XtAddCallback (buttonWidget, XtNcallback, continuationButtonCallback,
       (XtPointer)this);
      continuationButtons.push_back (new xxWidget (buttonWidget));
      westWidget = buttonWidget;
    }
  }
}


void xxLocationList::reconfigure() {
  switch (sortChoice->choice()) {
  case 0:
    _sortKey = StationIndex::sortByName;
    break;
  case 1:
    _sortKey = StationIndex::sortByLat;
    break;
  case 2:
    _sortKey = StationIndex::sortByLng;
    break;
  default:
    assert (false);
  }
  _stationIndex->sort (_sortKey);
  refreshList();
}


xxLocationList::xxLocationList (const xxWidget &parent,
				StationIndex *stationIndex,
                                xxWindow *globeOrMap):
  xxWindow (parent, formContainer),
  _globeOrMap(globeOrMap),
  _stationIndex(stationIndex),
  startAt(0),
  _sortKey(StationIndex::sortByName) {

  setTitle ("Location List");

  Arg labelArgs[8] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {(char*)XtNleft, (XtArgVal)XawChainLeft},
    {(char*)XtNright, (XtArgVal)XawChainLeft},
    {(char*)XtNtop, (XtArgVal)XawChainTop},
    {(char*)XtNbottom, (XtArgVal)XawChainTop},
    {XtNjustify, (XtArgVal)XtJustifyRight},
    {fontArgName, xxX::monoFontArgValue}
  };
  Widget labelWidget = xxX::createXtWidget (
"Location Name                                                           \
         Type Latitude Longitude",
    labelWidgetClass, container->widget(), labelArgs, 8);
  label = xxX::wrap (labelWidget);

  Arg vpArgs[10] =  {
    {XtNheight, (XtArgVal)locListViewportInitialHeight},
    {(char*)XtNfromVert, (XtArgVal)labelWidget},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {XtNallowVert, (XtArgVal)1},
    {XtNforceBars, (XtArgVal)1},
    {(char*)XtNleft, (XtArgVal)XawChainLeft},
    {(char*)XtNright, (XtArgVal)XawChainRight},
    {(char*)XtNtop, (XtArgVal)XawChainTop},
    {(char*)XtNbottom, (XtArgVal)XawChainBottom}
  };

  Widget viewportWidget = xxX::createXtWidget ("", viewportWidgetClass,
    container->widget(), vpArgs, 10);
  viewport = xxX::wrap (viewportWidget);
  xxX::fixBorder (viewportWidget);

  require (scrollbarWidget = XtNameToWidget (viewportWidget, "vertical"));
  Arg sbArgs[2] = {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  XtSetValues (scrollbarWidget, sbArgs, 2);
  setRudeness (scrollbarWidget);

  // Fix viewport width and align label with viewport text.
  Dimension labelWidth=0, scrollbarWidth=0, shadowWidth=0;
  XtVaGetValues (labelWidget, XtNwidth, (XtArgVal)(&labelWidth), NULL);
  XtVaGetValues (scrollbarWidget, XtNwidth, (XtArgVal)(&scrollbarWidth),
#ifdef HAVE_XAW3D
		 XtNshadowWidth, (XtArgVal)(&shadowWidth),
#endif
		 NULL);
  const Dimension correctWidth (labelWidth+scrollbarWidth+4*shadowWidth);
  XtVaSetValues (viewportWidget, XtNwidth, (XtArgVal)correctWidth, NULL);
  XtVaSetValues (labelWidget, XtNwidth, (XtArgVal)(correctWidth-shadowWidth), NULL);

  Arg listArgs[6] = {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {(char*)XtNverticalList, (XtArgVal)1},
    {(char*)XtNdefaultColumns, (XtArgVal)1},
    {(char*)XtNforceColumns, (XtArgVal)1},
    {fontArgName, xxX::monoFontArgValue}
  };
  Widget listWidget = xxX::createXtWidget ("", listWidgetClass,
    viewport->widget(), listArgs, 6);
  list = xxX::wrap (listWidget);
  XtAddCallback (listWidget, XtNcallback, xxLocationListChoiceCallback,
    (XtPointer)this);

  // It's a pain to install the list after the fact, but we need these params
  // to know what the correct length should be.  Assume shadowWidth same
  // on all widgets.
  Dimension internalHeight=2, rowSpacing=2;
  XtVaGetValues (listWidget, XtNinternalHeight, (XtArgVal)(&internalHeight),
		 XtNrowSpacing, (XtArgVal)(&rowSpacing), NULL);
  maxLocListLength = (32765U - 2 * (internalHeight + shadowWidth)) /
    (xxFontHeight(xxX::monoFontStruct) + rowSpacing);
  stringList = _stationIndex->makeStringList(startAt, maxLocListLength);
  listChanged();

  {
    static constString sortChoices[] = {"Sort by Name",
	  			        "Sort by Latitude",
				        "Sort by Longitude",
				        NULL};
    Arg layoutArgs[5] =  {
      {(char*)XtNfromVert, (XtArgVal)viewportWidget},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    sortChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*container,
								  sortChoices,
								  0,
								  *this,
								  layoutArgs,
								  5));
  }

  {
    Arg buttonArgs[8] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromHoriz, (XtArgVal)sortChoice->widget()},
      {(char*)XtNfromVert, (XtArgVal)viewportWidget},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonWidget = xxX::createXtWidget ("?", commandWidgetClass,
      container->widget(), buttonArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxLocationListHelpCallback,
      (XtPointer)this);
    helpButton = xxX::wrap (buttonWidget);
  }

  doContinuationButtons();

  obeyMouseWheel (popup->widget());
  obeyMouseWheel (listWidget);
  obeyMouseWheel (scrollbarWidget);

  realize();
  widthNoSmaller();
}


void xxLocationList::freeStringList() {
  unsigned long i=0;
  while (stringList[i])
    free (stringList[i++]);
  free (stringList);
}


void xxLocationList::changeList (StationIndex *stationIndex) {
  _stationIndex = std::auto_ptr<StationIndex> (stationIndex);
  // These always arrive in alphabetical order because the master
  // list stays in alphabetical order.
  if (_sortKey != StationIndex::sortByName)
    _stationIndex->sort (_sortKey);
  refreshList();
}


void xxLocationList::refreshList() {
  freeStringList();
  startAt = 0;
  stringList = _stationIndex->makeStringList(startAt, maxLocListLength);
  listChanged();
  doContinuationButtons();
}


xxLocationList::~xxLocationList () {
  unrealize();
  for (SafeVector<xxWidget*>::iterator it = continuationButtons.begin();
       it != continuationButtons.end();
       ++it)
    delete *it;
  freeStringList();
}


void xxLocationList::globalRedraw() {
  xxWindow::globalRedraw();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (helpButton.get());
  XtSetValues (helpButton->widget(), buttonArgs, 2);
  assert (sortChoice.get());
  sortChoice->globalRedraw();
  for (SafeVector<xxWidget*>::iterator it = continuationButtons.begin();
       it != continuationButtons.end();
       ++it)
    XtSetValues ((*it)->widget(), buttonArgs, 2);
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label.get());
  XtSetValues (label->widget(), args, 2);
  assert (list.get());
  XtSetValues (list->widget(), args, 2);
  assert (viewport.get());
  XtSetValues (viewport->widget(), args, 2);
  XtSetValues (scrollbarWidget, args, 2);
}

// Cleanup2006 Done
