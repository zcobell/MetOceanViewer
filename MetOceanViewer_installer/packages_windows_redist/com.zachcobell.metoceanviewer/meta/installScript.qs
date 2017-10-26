function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/MetOceanViewer.exe", "@StartMenuDir@/MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
        component.addOperation("CreateShortcut", "@TargetDir@/MetOceanViewer.exe", "@DesktopDir@/MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
        component.addOperation("CreateShortcut", "@TargetDir@/MetOceanViewer_MaintenanceTool.exe", "@StartMenuDir@/Uninstall MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
    }
}
