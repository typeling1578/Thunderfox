(function(){

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

// Get this in a way where we can load the page automatically
// where it doesn't need to be focused...
var homeWindow = Cc["@mozilla.org/embedcomp/window-watcher;1"]
    .getService(Ci.nsIWindowWatcher)
    .activeWindow;

var consoleService = Cc["@mozilla.org/consoleservice;1"]
    .getService(Components.interfaces.nsIConsoleService);

var extensionManager = Cc["@mozilla.org/extensions/manager;1"]  
    .getService(Ci.nsIExtensionManager);  

var Utils = {
  // ___ Windows and Tabs
  get activeWindow(){
    var win = Cc["@mozilla.org/embedcomp/window-watcher;1"]
               .getService(Ci.nsIWindowWatcher)
               .activeWindow;
               
    if( win != null ) return win;  
    else return homeWindow;
  },
  
  get activeTab(){
    var tabBrowser = this.activeWindow.gBrowser;
    return tabBrowser.selectedTab;
  },
  
  get homeTab(){
    for( var i=0; i<Tabs.length; i++){
      if(Tabs[i].contentWindow.location.host == "tabcandy"){
        return Tabs[i];
      }
    }
    
    return null;
  },
  
  // ___ Files
  getInstallDirectory: function(id) { 
    var file = extensionManager.getInstallLocation(id).getItemFile(id, "install.rdf");  
    return file.parent;  
  }, 
  
  getFiles: function(dir) {
    var files = [];
    if(dir.isReadable() && dir.isDirectory) {
      var entries = dir.directoryEntries;
      while(entries.hasMoreElements()) {
        var entry = entries.getNext();
        entry.QueryInterface(Ci.nsIFile);
        files.push(entry);
      }
    }
    
    return files;
  },

  getVisualizationNames: function() {
    var names = [];
    var dir = this.getInstallDirectory('tabcandy@aza.raskin');  
    dir.append('content');
    dir.append('candies');
    var files = this.getFiles(dir);
    var count = files.length;
    var a;
    for(a = 0; a < count; a++) {
      var file = files[a];
      if(file.isDirectory()) 
        names.push(file.leafName);
    }

    return names;
  },
    
  // ___ Logging
  
  // Interactive logging!
  ilog: function(){ // pass as many arguments as you want, it'll print them all
    // If Firebug lite already exists, print to the console.
    if( window.firebug ){
      window.firebug.d.console.cmd.log.apply(null, arguments);
      return;
    }
    
    // Else, embed it.
    $('<link rel="stylesheet" href="../../js/firebuglite/firebug-lite.css"/>')
      .appendTo("head");
    
    $('<script src="../../js/firebuglite/firebug-lite.js"></script>')
      .appendTo("body");
    
    var args = arguments;
    
    (function(){
      var fb = window.firebug;
      if(fb && fb.version){
        fb.init();
        fb.win.setHeight(100);
        fb.d.console.cmd.log.apply(null, args);
        }
      else{setTimeout(arguments.callee);}
    })();
  },
  
  log: function() { // pass as many arguments as you want, it'll print them all
    var text = this.expandArgumentsForLog(arguments);
    consoleService.logStringMessage(text);
  }, 
  
  error: function() { // pass as many arguments as you want, it'll print them all
    var text = this.expandArgumentsForLog(arguments);
    Cu.reportError('tabcandy error: ' + text);
  }, 
  
  trace: function() { // pass as many arguments as you want, it'll print them all
    var text = this.expandArgumentsForLog(arguments);
    if(typeof(printStackTrace) != 'function')
      this.log(text + ' trace: you need to include stacktrace.js');
    else {
      var calls = printStackTrace();
      calls.splice(0, 3); // Remove this call and the printStackTrace calls
      this.log('trace: ' + text + '\n' + calls.join('\n'));
    }
  }, 
  
  assert: function(label, condition) {
    if(!condition) {
      var text = 'tabcandy assert: ' + label;        
      if(typeof(printStackTrace) == 'function') {
        var calls = printStackTrace();
        text += '\n' + calls[3];
      }
      
      Cu.reportError(text);
    }
  },
  
  expandObject: function(obj) {
      var s = obj + ' = {';
      for(prop in obj) {
        var value = obj[prop]; 
        s += prop + ": " 
        + (typeof(value) == 'string' ? '\'' : '')
        + value 
        + (typeof(value) == 'string' ? '\'' : '')
        + ", ";
      }
      return s + '}';
    }, 
    
  expandArgumentsForLog: function(args) {
    var s = '';
    var count = args.length;
    var a;
    for(a = 0; a < count; a++) {
      var arg = args[a];
      if(typeof(arg) == 'object')
        arg = this.expandObject(arg);
        
      s += arg;
      if(a < count - 1)
        s += '; ';
    }
    
    return s;
  },
  
  testLogging: function() {
    this.log('beginning logging test'); 
    this.error('this is an error');
    this.trace('this is a trace');
    this.log(1, null, {'foo': 'hello', 'bar': 2}, 'whatever');
    this.log('ending logging test');
  }, 
  
  // ___ Event
  isRightClick: function(event) {
    if(event.which)
      return (event.which == 3);
    else if(event.button) 
      return (event.button == 2);
    
    return false;
  },
  
  // ___ Time
  getMilliseconds: function() {
  	var date = new Date();
  	return date.getTime();
  }     
};

window.Utils = Utils;

})();
