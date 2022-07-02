function inject() {
    setInterval(function () {
        window["_lact"] = Date.now();
    }, 300000);

    if (location.hostname == "www.youtube.com") {
        let elem = null;
        setInterval(function () {
            if(!elem) {
                elem = document.querySelector('ytd-watch-flexy');
            }
            if (elem) {
                elem.youthereDataChanged_ = function () { };
                elem.youThereManager_.youThereData_ = null;
            }
        }, 2000)
    }
}
let elem = document.createElement("script");
elem.innerText = "(" + inject.toString() + "())";
document.documentElement.appendChild(elem)
