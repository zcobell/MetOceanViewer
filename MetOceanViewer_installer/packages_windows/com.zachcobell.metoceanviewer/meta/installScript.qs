function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/MetOcean_Viewer.exe", "@StartMenuDir@/MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
        component.addOperation("CreateShortcut", "@TargetDir@/MetOcean_Viewer.exe", "@DesktopDir@/MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
        component.addOperation("CreateShortcut", "@TargetDir@/MetOceanViewer_MaintenanceTool.exe", "@StartMenuDir@/Uninstall MetOceanViewer.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/mov.ico");
        component.addOperation("Execute", "@TargetDir@/vcredist_x64.exe", "/quiet", "/norestart");
    }
}
