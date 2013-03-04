window.jQuery(function($) {



function setCookie(c_name, value, expiredays)
{
	var exdate = new Date();
	exdate.setDate(exdate.getDate() + expiredays);
	document.cookie = c_name + "=" + escape(value) + ((expiredays == null) ? "" : ";path=/;expires=" + exdate.toUTCString());
}

function getCookie(c_name)
{
	if (document.cookie.length > 0)
	{
		c_start = document.cookie.indexOf(c_name + "=");
		if (c_start != -1)
		{
			c_start = c_start + c_name.length+1;
			c_end = document.cookie.indexOf(";",c_start);
			if (c_end == -1)
			{
				c_end = document.cookie.length;
			}
			return unescape(document.cookie.substring(c_start, c_end));
		}
	}
	return "";
}


NAVI = new Object();

NAVI.CloseTimer = null;

NAVI.Open = function ( menu_tag, dir )
{
  NAVI_CancelTimer();
  NAVI_Close();
  SEARCH_CancelTimer();
  SEARCH_Close();
  if ('h' == dir)
  {
    return;
  }
  var pos = $("#navilink-span-"+menu_tag).offset();
  if (dir=='r')
  {
    $("#navi-dropdown-"+menu_tag).css( { "position": "absolute", "left": (pos.left + ($("#navilink-span-"+menu_tag).width()) + 2 - ($("#navi-dropdown-"+menu_tag).width())) + "px", "top": (pos.top + 32) + "px" } );
  }
  else
  {
    $("#navi-dropdown-"+menu_tag).css( { "position": "absolute", "left": (pos.left) + "px", "top": (pos.top + 32) + "px" } );
  }
  $("#navi-dropdown-"+menu_tag).show();
  $("#navilink-span-"+menu_tag).bind('mouseover',NAVI_CancelTimer);
  $("#navilink-span-"+menu_tag).bind('mouseout',NAVI_Timer);
  $("#navi-dropdown-"+menu_tag).bind('mouseover',NAVI_CancelTimer);
  $("#navi-dropdown-"+menu_tag).bind('mouseout',NAVI_Timer);
};

function NAVI_Close()
{
  $(".navi-dropdown").hide();
  $("#menu div").unbind('mouseover');
  $(".dropdown").unbind('mouseover');
  $(".dropdown").unbind('mouseout');

  $("#search-dropdown").hide();
  $("#searchlink-anchor").unbind('mouseover');
  $("#searchlink-anchor").unbind('mouseout');
  $("#search-dropdown").unbind('mouseover');
  $("#search-dropdown").unbind('mouseout');
}

function NAVI_CancelTimer()
{
  if (NAVI.CloseTimer!=null)
  {
    window.clearTimeout(NAVI.CloseTimer);
    NAVI.CloseTimer = null;
  }
}

function NAVI_Timer()
{
  if (NAVI.CloseTimer==null)
  {
    NAVI.CloseTimer = window.setTimeout(NAVI_Close, 300);
  }
}


SUBNAVI = new Object();

SUBNAVI.Open = function ( menu_tag )
{
  if ($("#sidesubnavi-" + menu_tag + ':hidden').length)
  {
    $(".sidesubnavi").hide();
    $("#sidesubnavi-"+menu_tag).show();
    return false;
  } else {
    return true;
  }
};


SEARCH = new Object();

SEARCH.CloseTimer = null;

SEARCH.Open = function()
{
  NAVI_CancelTimer();
  NAVI_Close();
  SEARCH_CancelTimer();
  SEARCH_Close();
  var pos = $("#searchlink-anchor").offset();
  $("#search-dropdown").css( { "position": "absolute", "left": (pos.left - ($("#search-dropdown").width()) + 40) + "px", "top": (pos.top + 36) + "px" } );
  $("#search-dropdown").show();
  $("#searchlink-anchor").bind('mouseover',NAVI_CancelTimer);
  $("#searchlink-anchor").bind('mouseout',NAVI_Timer);
  $("#search-dropdown").bind('mouseover',NAVI_CancelTimer);
  $("#search-dropdown").bind('mouseout',NAVI_Timer);
  $("#search-input")[0].focus();
};

function SEARCH_Close()
{
  $(".navi-dropdown").hide();
  $("#menu div").unbind('mouseover');
  $(".dropdown").unbind('mouseover');
  $(".dropdown").unbind('mouseout');

  $("#search-dropdown").hide();
  $("#searchlink-anchor").unbind('mouseover');
  $("#searchlink-anchor").unbind('mouseout');
  $("#search-dropdown").unbind('mouseover');
  $("#search-dropdown").unbind('mouseout');
}


function SEARCH_CancelTimer()
{
  if (SEARCH.CloseTimer!=null)
  {
    window.clearTimeout(SEARCH.CloseTimer);
    SEARCH.CloseTimer = null;
  }
}

function SEARCH_Timer()
{
  if (SEARCH.CloseTimer==null)
  {
    SEARCH.CloseTimer = window.setTimeout(SEARCH_Close, 300);
  }
}

menuImg1 = new Image(); menuImg1.src = 'http://s1.percona.com/ui-dropdown-header-l.png';
menuImg2 = new Image(); menuImg2.src = 'http://s2.percona.com/ui-dropdown-header-r.png';
menuImg3 = new Image(); menuImg3.src = 'http://s3.percona.com/ui-dropdown-header-search.png';
menuImg4 = new Image(); menuImg4.src = 'http://s0.percona.com/ui-dropdown-bg.png';
menuImg5 = new Image(); menuImg5.src = 'http://s1.percona.com/ui-dropdown-footer.png';


});


var Percona = {
    ssl: false,
    host: 'www.percona.com'
};
/**
 * @param string selector jQuery selector string
 */
Percona.getRecentServerVersion = function(selector)
{
    if ('string' != typeof(selector))
    {
        alert('Percona.getRecentServerVersion: missed or wrong selector!');
    }
    /* Localize jQuery variable */
    var jQuery;
    /******** Load jQuery if not present *********/
    if (window.jQuery === undefined || window.jQuery.fn.jquery !== '1.4.2')
    {
        var script_tag = document.createElement('script');
        script_tag.setAttribute("type","text/javascript");
        script_tag.setAttribute('src', 'http' + (Percona.ssl ? 's' : '') + ':/' + '/ajax.googleapis.com/ajax/libs/jquery/1.4.2/jquery.min.js');
        script_tag.onload = scriptLoadHandler;
        script_tag.onreadystatechange = function () /* Same thing but for IE */
        {
            if (this.readyState == 'complete' || this.readyState == 'loaded')
            {
                scriptLoadHandler();
            }
        };
        /* Try to find the head, otherwise default to the documentElement */
        (document.getElementsByTagName("head")[0] || document.documentElement).appendChild(script_tag);
    } else {
        /* The jQuery version on the window is the one we want to use */
        jQuery = window.jQuery;
        main();
    }
    var scriptLoadHandler_counter = 0;
    /******** Called once jQuery has loaded ******/
    function scriptLoadHandler()
    {
        if (++scriptLoadHandler_counter > 1)
        {
            return;
        }
        /* Restore $ and window.jQuery to their previous values and store the
           new jQuery in our local jQuery variable */
        jQuery = window.jQuery.noConflict(true);
        /* Call our main function */
        main(jQuery);
    }
    /******** Our main function ********/
    function main($)
    {
        var fillRecentServerVersion = function($)
        {
            if ($(selector).get(0))
            {
                $.get('http' + (Percona.ssl ? 's' : '') + ':/' + '/' + Percona.host + '/ajax/server-version/?callback=?', {}, function(json)
                {
                    if ('object' == typeof(json) && 'string' == typeof(json.recentServerVersion))
                    {
                        $(selector).text(' ' + json.recentServerVersion);
                    }
                }, 'jsonp');
            }
        };
        $(document).ready(function()
        {
            fillRecentServerVersion(jQuery);
        });
    }
};

$(document).ready(function(){
	$(window).bind("resize", resizeWindow);
	resizeWindow();
	function resizeWindow() {
		var win_w = $(window).width();
		var ribon = $("#support-ribbon");
		if(win_w < 1265){
			if(/mobile/i.test(navigator.userAgent)){
				ribon.hide();
			}else{
				if(ribon.hasClass("vertical")){
					ribon.removeClass("vertical");
					ribon.addClass("horizontal");
				}
				ribon.css({"left":'50%', "margin-left": '-'+(ribon.width() / 2)+'px'});
			}
		}else{
			if(ribon.hasClass("horizontal")){
				ribon.addClass("vertical");
				ribon.removeClass("horizontal");
				ribon.removeAttr("style");
			}

		}
	}
});