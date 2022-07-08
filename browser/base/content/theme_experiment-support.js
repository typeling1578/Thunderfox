// Since the size of the URL bar is determined by JavaScript when the Window is opened, we will take measures to recalculate it a little later after the pref is updated.
{
    let callback = function(){
        try {
            setTimeout(function() {
                gURLBar._updateLayoutBreakoutDimensions();
            }, 100);
            setTimeout(function() {
                gURLBar._updateLayoutBreakoutDimensions();
            }, 500);
        } catch (e) {
            window.removeEventListener("windowlwthemeupdate", callback)
        }
    }
    window.addEventListener("windowlwthemeupdate", callback);    
}