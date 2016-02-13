#include <usgs.h>

int usgs::getDataBounds(double &ymin, double &ymax)
{
    int j;

    ymin =  999999999.0;
    ymax = -999999999.0;

    for(j=0;j<this->USGSPlot.length();j++)
    {
        if(this->USGSPlot[j].value<ymin)
            ymin = this->USGSPlot[j].value;
        if(this->USGSPlot[j].value>ymax)
            ymax = this->USGSPlot[j].value;
    }
    return 0;
}
