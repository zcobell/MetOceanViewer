// $Id: xxSimplePrompt.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxSimplePrompt  Generic application of dialogWidgetClass.

    Copyright (C) 2007  David Flater.

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

class xxSimplePrompt: public xxWindow {
public:
  xxSimplePrompt (const xxWidget &parent,
		  xxPredictionWindow &caller,
		  const Dstr &title);
  ~xxSimplePrompt();

  void callback();

protected:
  xxPredictionWindow &_caller;
  std::auto_ptr<xxWidget> dialog;

  // This is called from the constructor of the subclass to complete
  // construction once the help text and initial value have been
  // assembled.  (They are not always sufficiently trivial to embed in
  // an initializer.)
  void construct (const Dstr &helpText, const Dstr &initValue);

  virtual void callback (const Dstr &value) = 0;
};

// Cleanup2006 Done
