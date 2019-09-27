function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    desktopData="Version=4.4.0\nType=Application\nTerminal=false\nExec=@TargetDir@/MetOceanViewer\nName=MetOcean Viewer\nIcon=@TargetDir@/MetOceanViewer.png\nName[en_US]=MetOcean Viewer";
    component.createOperations();
    component.addOperation("CreateDesktopEntry", "@HomeDir@/.local/share/applications/MetOceanViewer.desktop",desktopData);
    component.addOperation("CreateDesktopEntry", "@HomeDir@/Desktop/MetOceanViewer.desktop",desktopData);
}
