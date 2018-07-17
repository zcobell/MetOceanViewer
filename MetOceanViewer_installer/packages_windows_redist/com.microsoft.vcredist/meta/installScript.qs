function Component()
{
    // default constructor
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("Execute", "{0,1638,3010}", "@TargetDir@/vc_redist.x64.exe","/norestart");
    }
}
