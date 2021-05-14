

// default includes
#include <Hash.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

//
// Pin Setup
//
const byte g = D3;
const byte y = D2;
const byte r = D1;

// initial Relais state
int state = 0;

// setup your default wifi ssid, passwd and dns stuff. Can overwriten also later from setup website
const int eepromStringSize = 24;     // maximal byte size for strings (ssid, wifi-password, dnsname)

char ssid[eepromStringSize]     = "";     // wifi SSID name
char password[eepromStringSize] = "";     // wifi wpa2 password
char dnsname[eepromStringSize]  = "esp";  // Uniq Networkname
char place[eepromStringSize]    = "";     // Place of the Device
int  port                       = 80;     // Webserver port (default 80)
bool silent                     = false;  // enable/disable silent mode
bool debug                      = true;   // enable/disable debug mode (should be enabled on first boot)

const char* history             = "4.2";  // Software Version
String      webString           = "";     // String to display
String      ip                  = "127.0.0.1";  // default ip, will overwriten
const int   eepromAddr          = 0;      // default eeprom Address
const int   eepromSize          = 512;    // default eeprom size in kB (see datasheet of your device)
const int   eepromchk           = 48;     // byte buffer for checksum
char        lastcheck[ eepromchk ] = "";  // last created checksum
int         configSize          = 0;      // Configuration Size in kB
String      currentRequest      = "";     // Value for the current request (used in debugOut)
unsigned long previousMillis    = 0;      // flashing led timer value
bool SoftAccOK                  = false;  // value for captvie portal function 
bool captiveCall                = false;  // value to ensure we have a captive request

// basic auth 
bool authentication             = false;  // enable/disable Basic Authentication
char authuser[eepromStringSize] = "";     // user name
char authpass[eepromStringSize] = "";     // user password

// token auth
bool tokenauth                  = false;  // enable/disable Tokenbased Authentication
char token[eepromStringSize]    = "";     // The token variable

// Secpush
bool secpush                    = true;   // enable/disable secpush (turn off auth after boot for n seconds)
int  secpushtime                = 300;    // default secpush time in Seconds
bool secpushstate               = false;  // current secpush state (false after event has trigger)

// Static IP
bool staticIP = false;                    // use Static IP
IPAddress ipaddr(192, 168, 1, 250);       // Device IP Adress
IPAddress gateway(192, 168, 1, 1);        // Gateway IP Adress
IPAddress subnet(255, 255, 255, 0);       // Network/Subnet Mask
IPAddress dns1(192, 168, 1, 1);           // First DNS
IPAddress dns2(4, 4, 8, 8);               // Second DNS
String dnssearch;                         // DNS Search Domain

// DNS server
const byte DNS_PORT = 53;                 // POrt of the DNS Server
DNSServer dnsServer;                      // DNS Server port

ESP8266WebServer server( port );          // Webserver port


//
// Ampelcode
//

// set relais Function (inverted switch)
void ampelSet() {
  digitalWrite( r, HIGH );
  digitalWrite( y, HIGH );
  digitalWrite( g, HIGH );
  if( state == 1 ) { // red on
    digitalWrite( r, LOW );
    digitalWrite( y, HIGH );
    digitalWrite( g, HIGH );
  } else if( state == 2 ) { // red and yellow on
    digitalWrite( r, LOW );
    digitalWrite( y, LOW );
    digitalWrite( g, HIGH );
  } else if( state == 3 ) { // green on
    digitalWrite( r, HIGH );
    digitalWrite( y, HIGH );
    digitalWrite( g, LOW );
  } else if( state == 4 ){ // yellow on
    digitalWrite( r, HIGH );
    digitalWrite( y, LOW );
    digitalWrite( g, HIGH );
  } else { // all off
    digitalWrite( r, HIGH );
    digitalWrite( y, HIGH );
    digitalWrite( g, HIGH );
  }
}


//
// Helper Functions
//

// http response handler
void response( String& data, String requesttype ) { // , String addHeaders 
  if( ! silent ) digitalWrite( LED_BUILTIN, LOW );
  // get the pure dns name from requests
  if( dnssearch.length() == 0 ) { // only if not in AP Mode  ! SoftAccOK &&
    if( ! isIp( server.hostHeader() ) ) {
      String fqdn = server.hostHeader(); // esp22.speedport.ip
      int hostnameLength = String( dnsname ).length() + 1; // 
      fqdn = fqdn.substring( hostnameLength );
      String tmpPort = ":" + String( port );
      fqdn.replace( tmpPort, "" );
      dnssearch = fqdn;
    }
  }
//  if( addHeaders != null || addHeaders != "" ) {
//    server.sendHeader("Referrer-Policy","origin"); // Set aditional header data
//    server.sendHeader("Access-Control-Allow-Origin", "*"); // allow 
//  }
  server.send( 200, requesttype, data );
  delay(300);
  if( ! silent ) digitalWrite( LED_BUILTIN, HIGH );
}

// debug output to serial
void debugOut( String caller ) {
//  String callerUpper = caller;
//  callerUpper.toUpperCase();
//  char callerFirst = callerUpper.charAt(0);
//  caller = String( callerFirst ) + caller.substring(1,-1);
//  Serial.println( caller );
  if( debug ) {
    Serial.println( " " ); // Add newline
    Serial.print( "* " );
    Serial.print( server.client().remoteIP() );
    Serial.print( " requesting " );
    Serial.println( server.uri() );
    if( SoftAccOK ) {
      Serial.print( "  Connected Wifi Clients: " );
      Serial.println( (int) WiFi.softAPgetStationNum() );
    }
  }
}


// load config from eeprom
bool loadSettings() {
  if( debug ) Serial.println( "- Functioncall loadSettings()" );
  // define local data structure
  struct { 
    char eepromssid[ eepromStringSize ];
    char eeprompassword[ eepromStringSize ];
    char eepromdnsname[ eepromStringSize ];
    char eepromplace[ eepromStringSize ];
    bool eepromauthentication = false;
    char eepromauthuser[eepromStringSize];
    char eepromauthpass[eepromStringSize];
    bool eepromtokenauth = false;
    char eepromtoken[eepromStringSize];
    bool eepromsecpush        = false;
    int  eepromsecpushtime    = 0;
    bool eepromsilent         = false;
    bool eepromdebug          = false;
    bool eepromstaticIP       = false;
    IPAddress eepromipaddr;
    IPAddress eepromgateway;
    IPAddress eepromsubnet;
    IPAddress eepromdns1;
    IPAddress eepromdns2;
    int eepromstate;
    char eepromchecksum[ eepromchk ] = "";
  } data;
  // read data from eeprom
  EEPROM.get( eepromAddr, data );
  // validate checksum
  String checksumString = sha1( String( data.eepromssid ) + String( data.eeprompassword ) + String( data.eepromdnsname ) + String( data.eepromplace ) 
        + String( (int) data.eepromauthentication ) + String( data.eepromauthuser ) + String( data.eepromauthpass ) 
        + String( (int) data.eepromtokenauth ) + String( data.eepromtoken ) 
        + String( (int) data.eepromsecpush ) + String( data.eepromsecpushtime ) 
        + String( (int) data.eepromsilent ) + String( (int) data.eepromdebug ) 
        + String( (int) data.eepromstaticIP ) + ip2Str( data.eepromipaddr ) + ip2Str( data.eepromgateway ) + ip2Str( data.eepromsubnet ) + ip2Str( data.eepromdns1 ) + ip2Str( data.eepromdns2 )
        );
  char checksum[ eepromchk ];
  checksumString.toCharArray(checksum, eepromchk); // write checksumString into checksum
  if( strcmp( checksum, data.eepromchecksum ) == 0 ) { // compare with eeprom checksum
    // passed, write config data to variables
    if( debug ) Serial.println( "  passed checksum validation" );
    strncpy( lastcheck, checksum, eepromchk );
    configSize = sizeof( data );
    // re-set runtime variables;
    strncpy( ssid, data.eepromssid, eepromStringSize );
    strncpy( password, data.eeprompassword, eepromStringSize );
    strncpy( dnsname, data.eepromdnsname, eepromStringSize );
    strncpy( place, data.eepromplace, eepromStringSize );
    authentication = data.eepromauthentication;
    strncpy( authuser, data.eepromauthuser, eepromStringSize );
    strncpy( authpass, data.eepromauthpass, eepromStringSize );
    tokenauth = data.eepromtokenauth;
    strncpy( token, data.eepromtoken, eepromStringSize );
    secpush = data.eepromsecpush;
    secpushtime = data.eepromsecpushtime;
    silent = data.eepromsilent;
    debug = data.eepromdebug;
    staticIP = data.eepromstaticIP;
    ipaddr = data.eepromipaddr;
    gateway = data.eepromgateway;
    subnet = data.eepromsubnet;
    dns1 = data.eepromdns1;
    dns2 = data.eepromdns2;
    state = data.eepromstate;
    strncpy( checksum, data.eepromchecksum, eepromchk );
    return true;
  }
  if( debug ) Serial.println( "  failed checksum validation" );
  return false;
}


// write config to eeprom
bool saveSettings() {
  if( debug ) Serial.println( "- Functioncall saveSettings()" );
  // define local data structure
  struct { 
    char eepromssid[ eepromStringSize ];
    char eeprompassword[ eepromStringSize ];
    char eepromdnsname[ eepromStringSize ];
    char eepromplace[ eepromStringSize ];
    bool eepromauthentication = false;
    char eepromauthuser[eepromStringSize];
    char eepromauthpass[eepromStringSize];
    bool eepromtokenauth = false;
    char eepromtoken[eepromStringSize];
    bool eepromsecpush        = false;
    int  eepromsecpushtime    = 0;
    bool eepromsilent         = false;
    bool eepromdebug          = false;
    bool eepromstaticIP       = false;
    IPAddress eepromipaddr;
    IPAddress eepromgateway;
    IPAddress eepromsubnet;
    IPAddress eepromdns1;
    IPAddress eepromdns2;
    int eepromstate;
    char eepromchecksum[ eepromchk ] = "";
  } data;
  // write real data into struct elements
  strncpy( data.eepromssid, ssid, eepromStringSize );
  strncpy( data.eeprompassword, password, eepromStringSize );
  strncpy( data.eepromdnsname, dnsname, eepromStringSize );
  strncpy( data.eepromplace, place, eepromStringSize );
  data.eepromauthentication = authentication;
  strncpy( data.eepromauthuser, authuser, eepromStringSize );
  strncpy( data.eepromauthpass, authpass, eepromStringSize );
  data.eepromtokenauth = tokenauth;
  strncpy( data.eepromtoken, token, eepromStringSize );
  data.eepromsecpush = secpush;
  data.eepromsecpushtime = secpushtime;
  data.eepromsilent = silent;
  data.eepromdebug = debug;
  data.eepromstaticIP = staticIP;
  data.eepromipaddr = ipaddr;
  data.eepromgateway = gateway;
  data.eepromsubnet = subnet;
  data.eepromdns1 = dns1;
  data.eepromdns2 = dns2;
  data.eepromstate = state;
  // create new checksum
  String checksumString = sha1( String( data.eepromssid ) + String( data.eeprompassword ) + String( data.eepromdnsname ) + String( data.eepromplace ) 
        + String( (int) data.eepromauthentication ) + String( data.eepromauthuser )  + String( data.eepromauthpass ) 
        + String( (int) data.eepromtokenauth ) + String( data.eepromtoken ) 
        + String( (int) data.eepromsecpush ) + String( data.eepromsecpushtime ) 
        + String( (int) data.eepromsilent ) + String( (int) data.eepromdebug ) 
        + String( (int) data.eepromstaticIP ) + ip2Str( data.eepromipaddr ) + ip2Str( data.eepromgateway ) + ip2Str( data.eepromsubnet ) + ip2Str( data.eepromdns1 ) + ip2Str( data.eepromdns2 )
        );
  char checksum[ eepromchk ];
  checksumString.toCharArray(checksum, eepromchk);
  strncpy( data.eepromchecksum, checksum, eepromchk );
  strncpy( lastcheck, checksum, eepromchk );
  if( debug ) { Serial.print( "  create new config checksum: " ); Serial.println( checksum ); }
  configSize = sizeof( data );
  // save filed struct into eeprom
  EEPROM.put( eepromAddr,data );
  // commit transaction and return the write result state
  bool eepromCommit = EEPROM.commit();
  if( eepromCommit ) {
    if( debug ) Serial.println( "  successfully write config to eeprom" );
  } else {
    if( debug ) Serial.println( "  failed to write config to eeprom" );
  }
  return eepromCommit;
}


// is String an ip?
bool isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

// IPAdress 2 charArray
String ip2Str( IPAddress ip ) {
  String s="";
  for (int i=0; i<4; i++) {
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  }
  return s;
}

// parsing Strings for bytes, str2ip or str2arp
// https://stackoverflow.com/a/35236734
// parseBytes(String, '.', byte[], 4, 10); // ip adress
// parseBytes(String, ':', byte[], 8, 16); // mac adress
void parseBytes( const char* str, char sep, byte* bytes, int maxBytes, int base ) {
  for( int i = 0; i < maxBytes; i++ ) {
    bytes[ i ] = strtoul( str, NULL, base );
    str = strchr( str, sep );
    if( str == NULL || *str == '\0' ) {
      break;
    }
    str++;
  }
}

// Disable Authenthification after booting for X seconds
bool securePush() {
  if( secpush && secpushstate ) {
    int tmpPushtime = ( secpushtime * 1000 );
    if( ( (int) secpushtime - ( ( millis() - secpushtime ) / 60 /60 ) ) <= 3 ) {
      secpushstate = false;
      return false;
    }
  }
  return true;
}


bool validToken( String requestToken ) {
  bool result = false;
  if( requestToken == token ) {
    result = true;
  }
  return result;
}

void authorisationHandler() {
  if( ! SoftAccOK ) { // disable validation in AP Mode
    if( ! secpushstate ) { // disable validation when Secpush state is active
      if( authentication || tokenauth ) { // only if authentifications are enabled
        bool authenticated = false;
        // basic auth (on all pages)
        if( authentication ) {
          bool authenticateUser = server.authenticate( authuser, authpass );
          if( ! authenticateUser ) {
            if( debug ) Serial.println( "  basic auth - request username & password" );
            return server.requestAuthentication();
          } else {
            if( debug ) Serial.println( "  basic auth - validation passed." );
            authenticated = true;
          }
        }
        // token auth (only on Special pages)
        if( currentRequest == "metrics" ) { // token auth only on sepecial pages || currentRequest == "restart" || currentRequest == "reset"
          if( tokenauth && ! authenticated ) { // disable, if user authenticated allready by basic auth
            if( server.hasHeader( "X-Api-Key" ) ) {
              String apikey = server.header( "X-Api-Key" );
              if( apikey == "" ) {
                if( debug ) Serial.println( "  tokenauth - empty X-Api-Key, validation failed. Send 401." );
                return server.send(401, "text/plain", "401: Unauthorized, empty X-Api-Key.");
              }
              if( debug ) { Serial.print( "  tokenauth - got X-Api-Key (header): " ); Serial.println( apikey ); }
              if( validToken( apikey ) ) {
                if( debug ) Serial.println( "  tokenauth - validation passed" );
                authenticated = true;
              } else {
                if( debug ) Serial.println( "  tokenauth - invalid X-Api-Key, validation failed" );
                return server.send(401, "text/plain", "401: Unauthorized, wrong X-Api-Key. Send 401.");
              }
            } else if( server.arg( "apikey" ) ) {
              String apikey = server.arg( "apikey" );
              if( apikey == "" ) {
                if( debug ) Serial.println( "  tokenauth - empty apikey, validation failed. Send 401." );
                return server.send(401, "text/plain", "401: Unauthorized, empty apikey.");
              }
              if( debug ) { Serial.print( "  tokenauth - got apikey (arg): " ); Serial.println( apikey ); }
              if( validToken( apikey ) ) {
                if( debug ) Serial.println( "  tokenauth - validation passed." );
                authenticated = true;
              } else {
                if( debug ) Serial.println( "  tokenauth - invalid apikey, validation failed. Send 401." );
                return server.send(401, "text/plain", "401: Unauthorized, wrong apikey.");
              }
            } else {
              if( debug ) Serial.println( "  tokenauth - apikey or  X-Api-Key missing in request. Send 401." );
              return server.send(401, "text/plain", "401: Unauthorized, X-Api-Key or apikey missing.");
            }
          }
        } else { // currentRequested page dont use token auth
          if( debug ) { Serial.print( "  tokenauth - disabled uri: " ); Serial.println( currentRequest ); }
        }
      } else {
        if( debug ) Serial.println( "  authorisation disabled" );
      }
    } else { // secpush
      if( debug ) Serial.println( "  authorisation disabled by SecPush" );
    }
  } else { // softACC
    if( debug ) Serial.println( "  authorisation disabled in AP Mode" );
  }
}


//
// html helper functions
//

// spinner javascript
String spinnerJS() {
  String result( ( char * ) 0 );
  result.reserve( 440 ); // reserve 440 characters of Space into result
  result += "<script type='text/javascript'>\n";
  result += "var duration = 300,\n";
  result += "    element,\n";
  result += "    step,\n";
  result += "    frames = \"-\\\\/\".split('');\n";
  result += "step = function (timestamp) {\n";
  result += "  var frame = Math.floor( timestamp * frames.length / duration ) % frames.length;\n";
  result += "  if( ! element ) {\n";
  result += "    element = window.document.getElementById( 'spinner' );\n";
  result += "  }\n";
  result += "  element.innerHTML = frames[ frame ];\n";
  result += "  return window.requestAnimationFrame( step );\n";
  result += "}\n";
  result += "window.requestAnimationFrame( step );\n";
  result += "</script>\n";
  return result;
}


// spinner css
String spinnerCSS() {
  String result( ( char * ) 0 );
  result.reserve( 120 );
  result += "<style>\n";
  result += "#spinner_wrap {\n";
  result += "  margin-top: 25%;\n";
  result += "}\n";
  result += "#spinner {\n";
  result += "  font-size: 72px;\n";
  result += "  transition: all 500ms ease-in;\n";
  result += "}\n";
  result += "</style>\n";
  return result;
}


// global javascript
String htmlJS() {
  String result( ( char * ) 0 );
  result.reserve( 2750 );
  //String result = "";
  // javascript to poll wifi signal
  if( ! captiveCall && currentRequest == "/" ) { // deactivate if in setup mode (Captive Portal enabled)
    result += "window.setInterval( function() {\n"; // polling event to get wifi signal strength
    result += "  let xmlHttp = new XMLHttpRequest();\n";
    result += "  xmlHttp.open( 'GET', '/signal', false );\n";
    result += "  xmlHttp.send( null );\n";
    result += "  let signalValue = xmlHttp.responseText;\n";
    result += "  let signal = document.getElementById('signal');\n";
    result += "  signal.innerText = signalValue + ' dBm';\n";
    result += "}, 5000 );\n";
  }
  if( currentRequest == "networksetup" || currentRequest == "devicesetup" || currentRequest == "authsetup" ) {
    result += "  function renderRows( checkbox, classname ) {\n";
    result += "    if( checkbox === null || checkbox === undefined || checkbox === \"\" || classname === null || classname === undefined || classname === \"\" ) return;\n";
    result += "    let rows = document.getElementsByClassName( classname );\n";
    result += "    if( rows === null || rows === undefined || rows === \"\" ) return;\n";
    result += "    if( checkbox.checked == true ) {\n";
    result += "      for (i = 0; i < rows.length; i++) {\n";
    result += "        rows[i].style.display = 'table-row';\n";
    result += "      }\n";
    result += "    } else {\n";
    result += "      for (i = 0; i < rows.length; i++) {\n";
    result += "        rows[i].style.display = 'none';\n";
    result += "      }\n";
    result += "    }\n";
    result += "  }\n";
    if( currentRequest == "devicesetup" ) {
      result += "  function setAmpel( state ) {\n";
      result += "    if( state == undefined ) {\n";
      result += "      let ampelField = document.getElementById('ampelstate');\n";
      result += "      state = ampelField.value;\n";
      result += "    }\n";
      result += "    let xmlHttp = new XMLHttpRequest();\n";
      result += "    xmlHttp.open( 'GET', '/ampel?state=' + state, false );\n";
      result += "    xmlHttp.send( null );\n";
      result += "  }\n";
    }
    if( currentRequest == "authsetup" ) {
      result += "  function createToken( length ) {\n";
      result += "    var result           = '';\n";
      result += "    var characters       = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';\n";
      result += "    var charactersLength = characters.length;\n";
      result += "    for ( var i = 0; i < length; i++ ) {\n";
      result += "      result += characters.charAt(Math.floor(Math.random() * charactersLength));\n";
      result += "    }\n";
      result += "    return result;\n";
      result += "  }\n";
      result += "  function generateToken( tokenFildId ) {\n";
      result += "    let tokenField = document.getElementById( tokenFildId );\n";
      result += "    if( tokenField.value == null || tokenField.value == undefined ) return;\n";
      result += "    if( tokenField.value != \"\" ) {\n";
      result += "      let confirmed = window.confirm('Reset current Token?')\n"; 
      result += "      if( confirmed ) tokenField.value = createToken( 16 );\n";
      result += "    } else {\n";
      result += "      tokenField.value = createToken( 16 );\n";
      result += "    }\n";
      result += "  }\n";
      result += "  function toggleVisibility( pwFieldId ) {\n";
      result += "    let pwField = document.getElementById( pwFieldId );\n";
      result += "    if( pwField.type == 'text' ) {\n";
      result += "      pwField.type = 'password';\n";
      result += "    } else {\n";
      result += "      pwField.type = 'text';\n";
      result += "    }\n";
      result += "  }\n";
    }
    result += "  window.onload = function () {\n";
    if( currentRequest == "networksetup" ) result += "    renderRows( document.getElementById( 'staticIP'), \"staticIPRow\" );\n";
    if( currentRequest == "devicesetup" ) {}
    if( currentRequest == "authsetup" ) {
      result += "    renderRows( document.getElementById( 'authentication'), \"authRow\" );\n";
      result += "    renderRows( document.getElementById( 'tokenauth'), \"tokenAuthRow\" );\n";
      result += "    renderRows( document.getElementById( 'secpush'), \"secPushRow\" );\n";
      result += "    renderRows( document.getElementById( 'https'), \"httpsRow\" );\n";
    }
    result += "  };\n";
  }
  return result;
}


// global css
String htmlCSS() {
  //String result = "";
  String result( ( char * ) 0 );
  result.reserve( 700 ); // reseve space for N characters
  result += "@charset 'UTF-8';\n";
  result += "#signalWrap { float: right; }\n";
  result += "#signal { transition: all 500ms ease-out; }\n";
//  result += "#mcpChannelsRow, .staticIPRow, .authRow {\n";
//  result += "  transition: all 500ms ease-out;\n";
//  result += "  display: table-row;\n";
//  result += "}\n";
  result += "#footer { text-align:center; }\n";
  result += "#footer .right { color: #ccc; float: right; margin-top: 0; }\n";
  result += "#footer .left { color: #ccc; float: left; margin-top: 0; }\n";
  result += "@media only screen and (max-device-width: 720px) {\n";
  result += "  .links { background: red; }\n";
  result += "  .links tr td { padding-bottom: 15px; }\n";
  result += "  h1 small:before { content: \"\\A\"; white-space: pre; }\n";
  result += "  #signalWrap { margin-top: 15px; }\n";
  result += "  #spinner_wrap { margin-top: 45%; }\n";
  result += "  #links a { background: #ddd; display: block; width: 100%; min-width: 300px; min-height: 40px; text-align: center; vertical-align: middle; padding-top: 20px; border-radius: 10px; }\n";
  result += "  #footer .left, #footer .right { float: none; }\n";
  result += "  #links tr td:nth-child(2n) { display: none; }\n";
  result += "}\n";
  return result;
}

// html head
String htmlHead() {
  String result( ( char * ) 0 );
  result.reserve( 5500 );
  result += "<head>\n";
  result += "  <title>" + String( dnsname ) + "</title>\n";
  result += "  <meta http-equiv='content-type' content='text/html; charset=UTF-8'>\n";
  result += "  <meta name='viewport' content='width=device-width, minimum-scale=1.0, maximum-scale=1.0'>\n";
  result += "  <script type='text/javascript'>\n" + htmlJS() + "\n</script>\n";
  result += "  <style>\n" + htmlCSS() + "\n</style>\n";
  result += "</head>\n";
  return result;
}

// html header
String htmlHeader() {
  String result( ( char * ) 0 );
  result.reserve( 170 );
  result += "<div id='signalWrap'>Signal Strength: <span id='signal'>" + String( WiFi.RSSI() ) + " dBm</span></div>\n"; 
  result += "<h1>" + String( dnsname ) + " <small style='color:#ccc'>a D1Mini Node Exporter</small></h1>\n";
  result += "<hr />\n";
  return result;
}

// html footer
String htmlFooter() {
  String result( ( char * ) 0 );
  result.reserve( 250 );
  result += "<hr style='margin-top:40px;' />\n";
  result += "<div id='footer'>\n";
  result += "  <p class='right'>";
  result += staticIP ? "Static" : "Dynamic";
  result += " IP: " + ip + "</p>\n";
  result += "  <p class='left'><strong>S</strong>imple<strong>ESP</strong> v" + String( history ) + "</p>\n";
  result += "  <p>source: <a href='https://github.com/vaddi/d1mini_node' target='_blank'>github.com</a></p>\n";
  result += "</div>\n";
  return result;
}

// html body (wrapping content)
String htmlBody( String& content ) {
  String result = "";
  result += "<!DOCTYPE html>\n";
  result += "<html lang='en'>\n";
  result += htmlHead();
  result += "<body>\n";
  result += htmlHeader();
  result += content;
  result += htmlFooter();
  result += "</body>\n";
  result += "</html>";
  return result;
}

//
// HTML Handler Functions
//

// redirect client to captive portal after connect to wifi
boolean captiveHandler() {
  if( SoftAccOK && ! isIp( server.hostHeader() ) && server.hostHeader() != ( String( dnsname ) + "." + dnssearch ) ) {
    String redirectUrl = String("http://") + ip2Str( server.client().localIP() ) + String( "/network" );
    server.sendHeader("Location", redirectUrl, false );
    server.send( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    if( debug ) {
      Serial.print( "- Redirect from captiveHandler() to uri: " );
      Serial.println( redirectUrl );
    }
    server.client().stop(); // Stop is needed because we sent no content length
    captiveCall = true;
    return true;
  }
  captiveCall = false;
  return false;
}

// initial page
void handle_root() {
  if ( captiveHandler() ) { // If captive portal redirect instead of displaying the root page.
    return;
  }
  currentRequest = "/";
  debugOut( currentRequest );
  authorisationHandler();
  String result( ( char * ) 0 );
  result.reserve( 2000 );
  // create current checksum
  String checksumString = sha1( String( ssid ) + String( password ) + String( dnsname ) + String( place ) 
      + String( (int) authentication ) + String( authuser ) + String( authpass ) 
      + String( (int) tokenauth ) + String( token ) 
      + String( (int) secpush ) + String( secpushtime ) 
      + String( (int) silent ) + String( (int) debug ) 
      + String( (int) staticIP ) + ip2Str( ipaddr ) + ip2Str( gateway ) + ip2Str( subnet ) + ip2Str( dns1 ) + ip2Str( dns2 )
      );
  char checksum[ eepromchk ];
  checksumString.toCharArray(checksum, eepromchk); // write checksumString (Sting) into checksum (char Arr)
  result += "<h2>About</h2>\n";
  result += "<p>A Prometheus scrape ready Ampel Node Exporter.<br />\n";
  result += "Just add this Node to your prometheus.yml as Target to collect Lightstatus.</p>\n";
  result += "<h2>Links</h2>\n";
  result += "<table id='links'>\n";
  result += "  <tr><td><a href='/metrics";
  if( tokenauth ) { result += "?apikey=" + String( token ); }
  result += "'>/metrics</a></td><td></td></tr>\n";
  result += "  <tr><td><a href='/network'>/network</a></td><td>(Network Setup)</td></tr>\n";
  result += "  <tr><td><a href='/device'>/device</a></td><td>(Device Setup)</td></tr>\n";
  result += "  <tr><td><a href='/auth'>/auth</a></td><td>(Authentification Setup)</td></tr>\n";
  
  result += "  <tr><td><a href='/reset";
  if( tokenauth ) { result += "?apikey=" + String( token ); }
  result += "' onclick=\"return confirm('Reset the Device?');\">/reset</a></td><td>Simple reset</td></tr>\n";
  
  result += "  <tr><td><a href='/restart";
  if( tokenauth ) { result += "?apikey=" + String( token ); }
  result += "' onclick=\"return confirm('Restart the Device?');\">/restart</a></td><td>Simple restart</td></tr>\n";
  
  result += "  <tr><td><a href='/restart";
  if( tokenauth ) { result += "?apikey=" + String( token ) + "&"; }
    else { result += "?"; }
  result += "reset=1' onclick=\"return confirm('Reset the Device to Factory Defaults?');\">/restart?reset=1</a></td><td>Factory-Reset*</td></tr>\n";
  
  result += "</table>\n";
  result += "<p>* Clear EEPROM, will reboot in Setup (Wifi Acces Point) Mode named <strong>esp_setup</strong>!</p>";
  
  result += "<h2>Ampel Status</h2>\n";
  result += "<style>\n";
  result += "#ampel { border: 1px solid #000; font-size: 32pt; background: #333; }\n";
  result += "#ampel tr td { border-top: 1px solid #000; line-height: 26px; }\n";
  result += "#ampel tr:first-child td { border-top: none; }\n";
  result += "#ampel tr td a:link { text-decoration: none; }\n";
  result += "</style>\n";
  result += "<table id='ampelWrap'>\n";
  result += "  <tr>\n";
  result += "    <td>\n";
  result += "      <table id='ampel'>\n";
  if( state == 1 ) {
    // red on
    result += "        <tr><td><a href='ampel?state=1'><span style='color:#f00;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=4'><span style='color:#990;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=3'><span style='color:#090;'>&#9679;</span></a></td></tr>\n";
  } else if( state == 2 ) {
    // red and yellow on
    result += "        <tr><td><a href='ampel?state=1'><span style='color:#f00;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=4'><span style='color:#ff0;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=3'><span style='color:#090;'>&#9679;</span></a></td></tr>\n";
  } else if( state == 3 ) {
    // green on
    result += "        <tr><td><a href='ampel?state=1'><span style='color:#900;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=4'><span style='color:#990;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=3'><span style='color:#0f0;'>&#9679;</span></a></td></tr>\n";
  } else if( state == 4 ) {
    // yellow on
    result += "        <tr><td><a href='ampel?state=1'><span style='color:#900;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=4'><span style='color:#FF0;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=3'><span style='color:#090;'>&#9679;</span></a></td></tr>\n";
  } else {
    // all off
    result += "        <tr><td><a href='ampel?state=1'><span style='color:#900;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=4'><span style='color:#990;'>&#9679;</span></a></td></tr>\n";
    result += "        <tr><td><a href='ampel?state=3'><span style='color:#090;'>&#9679;</span></a></td></tr>\n";
  }
  result += "      </table>\n";
  result += "    </td>\n";
  result += "    <td>\n";
  result += "      <a style='padding-left:10px;' href='ampel?state=2'><button>Red + Yellow</button></a><br /><br />\n";
  result += "      <a style='padding-left:10px;' href='ampel?state=0'><button>All Off</button></a><br /><br />\n";
  result += "      <span style='padding-left:10px;'>Current State: " + String( state ) + "</span>\n";
  result += "    </td>\n";
  result += "  </tr>\n";
  result += "</table>\n";
  
  result += "<h2>Current Setup State</h2>\n";
  result += "<table>\n";
  result += "  <tr><td>DNS Search Domain: </td><td>" + dnssearch + "</td><td></td></tr>\n";
  // Silent
  if( silent ) {
    result += "  <tr><td>Silent Mode</td><td>Enabled</td><td>(LED disabled)</td></tr>\n";
  } else {
    result += "  <tr><td>Silent Mode</td><td>Disabled</td><td>(LED blink on Request)</td></tr>\n";
  }
  // Debug
  if( debug ) {
    result += "  <tr><td>Debug Mode</td><td>Enabled</td><td></td></tr>\n";
  } else {
    result += "  <tr><td>Debug Mode</td><td>Disabled</td><td></td></tr>\n";
  }
  // Authentification
  if( authentication ) {
    result += "  <tr><td>Basic Auth</td><td>Enabled</td><td></td></tr>\n"; // &#128274;
  } else {
    result += "  <tr><td>Basic Auth</td><td>Disabled</td><td></td></tr>\n"; // &#128275;
  }
  if( tokenauth ) {
    if( authentication ) {
      result += "  <tr><td>Token Auth</td><td>Enabled</td><td>Token: " + String( token ) + "</td></tr>\n"; // &#128273;
    } else {
      result += "  <tr><td>Token Auth</td><td>Enabled</td><td></td></tr>\n"; // &#128273;
    }
  }
  // SecPush
  if( secpush ) {
    result += "  <tr><td>SecPush</td><td>Enabled</td>";
    if( secpushstate ) {
      String currentSecPush = String( ( ( millis() - secpushtime ) / 60 /60 ) );
      currentSecPush += "/" + String( ( (int) secpushtime - ( ( millis() - secpushtime ) / 60 /60 ) ) );
      result += "<td>active (" + currentSecPush + " Seconcs, depart/remain)</td>";      
    } else {
      result += "<td>inactive</td>";
    }
    result += "</tr>\n";
  } else {
    result += "  <tr><td>SecPush</td><td>Disabled</td><td>";
    result += ( secpushstate ) ? "active" : "inactive";
    result += "</td></tr>\n";
  }
  // Checksum
  if( strcmp( checksum, lastcheck ) == 0 ) { // compare checksums
    result += "<tr><td>Checksum</td><td>Valid</td><td><span style='color:#66ff66;'>&#10004;</span></td></tr>\n";
  } else {
    result += "<tr><td>Checksum</td><td>Invalid</td><td><span style='color:#ff6666;'>&#10008;</span> Plaese <a href='/network'>Setup</a> your Device.</td></tr>\n";
  }
  result += "</table>\n";
  result =  htmlBody( result );
  response( result, "text/html" );
}


void ampelHandler () {
  currentRequest = "ampel";
  debugOut( currentRequest );
  authorisationHandler();
  // validate required args are set
  if( ! server.hasArg( "state" ) ) {
    server.send(400, "text/plain", "400: Invalid Request, Missing required state data.");
    return;
  }
  // validate args are not empty
  if( server.arg( "state" ) == "" ) {
    server.send(400, "text/plain", "400: Invalid Request, Empty state value detected.");
    return;
  }
  // write new state value to current config
  String stateString = server.arg("state");
  state = stateString.toInt();
  if( debug ) { Serial.println( "  set state to: " + stateString ); }
  ampelSet();
  // send user back to default page
  server.sendHeader( "Location","/" );
  server.send( 303 );
}


// 404 Handler
void notFoundHandler() {
  if ( captiveHandler() ) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  currentRequest = "404";
  debugOut( currentRequest );
  String result = "";
  result += "<div>";
  result += "  <p>404 Page not found.</p>";
  result += "  <p>Where sorry, but are unable to find page: <strong>" + String( server.uri() ) + "</strong></p>";
  result += "</div>";
  server.send( 404, "text/html", htmlBody( result ) );
}

// Signal Handler
void signalHandler() {
  //debugOut( "WiFiSignal" );
  webString = String( WiFi.RSSI() );
  response( webString, "text/plain" );
}

// Network Setup Handler
void networkSetupHandler() {
  currentRequest = "networksetup";
  debugOut( currentRequest );
  authorisationHandler();
  char activeState[10];
  String result = "";
  result += "<h2>Network Setup</h2>\n";
  result += "<div>";
  if( SoftAccOK ) {
    result += "  <p>Welcome to the initial Setup. You should tell this device you wifi credentials";
    result += "  and give em a uniqe Name. If no static IP is setup, the device will request an IP from a DHCP Server.</p>";
  } else {
    result += "  <p>Wifi and IP settings. Change the Nodes Wifi credentials or configure a static ip. If no static IP is ";
    result += "  setup, the device will request an IP from a DHCP Server.</p>\n";
  }
  result += "</div>\n";
  result += "<div>";
  result += "<form action='/networkform' method='POST' id='networksetup'>\n";
  result += "<table style='border:none;'>\n";
  result += "<tbody>\n";
  result += "  <tr><td><strong>Wifi Settings</strong></td><td></td></tr>\n"; // head line
  result += "  <tr>\n";
  result += "    <td><label for='ssid'>SSID<span style='color:red'>*</span>: </label></td>\n";
  result += "    <td><input id='ssid' name='ssid' type='text' placeholder='Wifi SSID' value='" + String( ssid ) + "' size='" + String( eepromStringSize ) + "' required /></td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td><label for='password'>Password<span style='color:red'>*</span>: </label></td>\n";
  result += "    <td><input id='password' name='password' type='password' placeholder='Wifi Password' value='" + String( password ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td><label for='dnsname'>DNS Name<span style='color:red'>*</span>: </label></td>\n";
  // ToDo validate for no whitespaces in the Name!
  result += "    <td><input id='dnsname' name='dnsname' type='text' placeholder='DNS Name' value='" + String( dnsname ) + "' size='" + String( eepromStringSize ) + "' required /> *avoid whitespaces!</td>\n";
  result += "  </tr>\n";
  strcpy( activeState, ( staticIP ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='staticIP'>Static IP: </label></td>\n";
  result += "    <td><input id='staticIP' name='staticIP' type='checkbox' onclick='renderRows( this, \"staticIPRow\" )' " + String( activeState ) + " /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='staticIPRow'>\n";
  result += "    <td><label for='ipaddr'>IP: </label></td>\n";
  result += "    <td><input id='ipaddr' name='ipaddr' type='text' placeholder='192.168.1.2' value='" + ip2Str( ipaddr ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='staticIPRow'>\n";
  result += "    <td><label for='gateway'>Gateway: </label></td>\n";
  result += "    <td><input id='gateway' name='gateway' type='text' placeholder='192.168.1.1' value='" + ip2Str( gateway ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='staticIPRow'>\n";
  result += "    <td><label for='subnet'>Subnet: </label></td>\n";
  result += "    <td><input id='subnet' name='subnet' type='text' placeholder='255.255.255.0' value='" + ip2Str( subnet ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='staticIPRow'>\n";
  result += "    <td><label for='dns1'>DNS: </label></td>\n";
  result += "    <td><input id='dns1' name='dns1' type='text' placeholder='192.168.1.1' value='" + ip2Str( dns1 ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='staticIPRow'>\n";
  result += "    <td><label for='dns2'>DNS 2: </label></td>\n";
  result += "    <td><input id='dns2' name='dns2' type='text' placeholder='8.8.8.8' value='" + ip2Str( dns2 ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line
  result += "  <tr>\n";
  result += "    <td></td><td><button name='submit' type='submit'>Submit</button></td>\n";
  result += "  </tr>\n";
  result += "</tbody>\n";
  result += "</table>\n";
  result += "</form>\n";
  result += "</div>\n";
  result += "<div>\n";
  result += "  <p><span style='color:red'>*</span>&nbsp; Required fields!</p>\n";
  result += "</div>\n";
  result += "<a href='/'>Zur&uuml;ck</a>\n";
  result = htmlBody( result );
  response( result, "text/html" );
}


// Network Form Handler
void networkFormHandler() {
  currentRequest = "networkform";
  debugOut( currentRequest );
  authorisationHandler();
    // validate required args are set
  if( ! server.hasArg( "ssid" ) || ! server.hasArg( "password" ) || ! server.hasArg( "dnsname" ) ) {
    server.send(400, "text/plain", "400: Invalid Request, Missing one of required form field field (ssid, password or dnsname).");
    return;
  }
  // validate args are not empty
  if( server.arg( "ssid" ) == "" || server.arg( "password" ) == "" || server.arg( "dnsname" ) == "" ) {
    server.send(400, "text/plain", "400: Invalid Request, Empty form field value detected.");
    return;
  }
  // write new values to current setup
  // Wifi Settings
  String ssidString = server.arg("ssid");
  ssidString.toCharArray(ssid, eepromStringSize);
  String passwordString = server.arg("password");
  passwordString.toCharArray(password, eepromStringSize);
  String dnsnameString = server.arg("dnsname");
  dnsnameString.toCharArray(dnsname, eepromStringSize);
  // Static IP stuff
  staticIP = server.arg("staticIP") == "on" ? true : false;
  String ipaddrString = server.arg("ipaddr");
  const char* ipStr = ipaddrString.c_str();
  byte tmpip[4];
  parseBytes(ipStr, '.', tmpip, 4, 10);
  ipaddr = tmpip;
  String gatewayString = server.arg("gateway");
  const char* gatewayStr = gatewayString.c_str();
  byte tmpgateway[4];
  parseBytes(gatewayStr, '.', tmpgateway, 4, 10);
  gateway = tmpgateway;
  String subnetString = server.arg("subnet");
  const char* subnetStr = subnetString.c_str();
  byte tmpsubnet[4];
  parseBytes(subnetStr, '.', tmpsubnet, 4, 10);
  subnet = tmpsubnet;
  String dns1String = server.arg("dns1");
  const char* dns1Str = dns1String.c_str();
  byte tmpdns1[4];
  parseBytes(dns1Str, '.', tmpdns1, 4, 10);
  dns1 = tmpdns1;
  String dns2String = server.arg("dns2");
  const char* dns2Str = dns2String.c_str();
  byte tmpdns2[4];
  parseBytes(dns2Str, '.', tmpdns2, 4, 10);
  dns2 = tmpdns2;
  // save data into eeprom
  bool result = saveSettings();
  delay(50);
  // send user to restart page
  String location = "/restart";
  if( tokenauth ) { location += "?apikey=" + String( token ); }
  server.sendHeader( "Location",location );
  server.send( 303 );
}

// Device Setup Handler
void deviceSetupHandler() {
  currentRequest = "devicesetup";
  debugOut( currentRequest );
  authorisationHandler();
  char activeState[10];
  String result = "";
  result += "<h2>Device Setup</h2>\n";
  result += "<div>";
  result += "  <p>Here you can configure all device specific stuff like connected Sensors or just set a place description.</p>";
  result += "</div>\n";
  result += "<form action='/deviceform' method='POST' id='devicesetup'>\n";
  result += "<table style='border:none;'>\n";
  result += "<tbody>\n";
  result += "  <tr><td><strong>Device Settings</strong></td><td></td></tr>\n"; // head line
  result += "  <tr>\n";
  result += "    <td><label for='place'>Place: </label></td>\n";
  result += "    <td><input id='place' name='place' type='text' placeholder='Place Description' value='" + String( place ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  strcpy( activeState, ( silent ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='silent'>Silent Mode: </label></td>\n";
  result += "    <td><input id='silent' name='silent' type='checkbox' " + String( activeState ) + "  /></td>\n";
  result += "  </tr>\n";
  strcpy( activeState, ( debug ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='debug'>Debug Mode: </label></td>\n";
  result += "    <td><input id='debug' name='debug' type='checkbox' onclick='renderGC()' " + String( activeState ) + "  /></td>\n";
  result += "  </tr>\n";
  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line

  // state
// 0 = all off
// 1 = red on
// 2 = red and yellow on
// 3 = green on
// 4 = yellow on
  result += "  <tr><td><strong>Ampel State</strong></td><td></td></tr>\n"; // head line
  result += "  <tr>\n";
  result += "    <td><label for='ampelstate'>Ampel initial State: </label></td>\n";
  result += "    <td><input id='ampelstate' name='ampelstate' type='number' onchange='setAmpel()' value='" + String( state ) + "' max='4' min='0' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td>0</td>\n";
  result += "    <td>All Off</td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td>1</td>\n";
  result += "    <td>Red = 1, Yellow = 0, Green = 0</td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td>2</td>\n";
  result += "    <td>Red = 1, Yellow = 1, Green = 0</td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td>3</td>\n";
  result += "    <td>Red = 0, Yellow = 0, Green = 1</td>\n";
  result += "  </tr>\n";
  result += "  <tr>\n";
  result += "    <td>4</td>\n";
  result += "    <td>Red = 0, Yellow = 1, Green = 0</td>\n";
  result += "  </tr>\n";

  result += "  <tr>\n";
  result += "    <td>URL</td>\n";
  result += "    <td>http://" + String( dnsname ) + "." + String( dnssearch ) + "/ampel?state=&lt;state&gt;</td>\n";
  result += "  </tr>\n";

  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line
  result += "  <tr>\n";
  result += "    <td></td><td><button name='submit' type='submit'>Submit</button></td>\n";
  result += "  </tr>\n";
  result += "</tbody>\n";
  result += "</table>\n";
  result += "</form>\n";
  result += "<a href='/'>Zur&uuml;ck</a>\n";
  result = htmlBody( result );
  response( result, "text/html" );
}

// Device Form Handler
void deviceFormHandler() {
  currentRequest = "deviceform";
  debugOut( currentRequest );
  authorisationHandler();
  // Sytem stuff
  silent = server.arg("silent") == "on" ? true : false;
  debug = server.arg("debug") == "on" ? true : false;
  // Place
  String placeString = server.arg("place");
  placeString.toCharArray(place, eepromStringSize);
  String stateString = server.arg("ampelstate");
  state = stateString.toInt();
  delay(50);
  // save data into eeprom
  bool result = saveSettings();
  delay(50);
  // send user to restart page
  String location = "/restart";
  if( tokenauth ) { location += "?apikey=" + String( token ); }
  server.sendHeader( "Location",location );
  server.send( 303 );
}

// Authentification Setup
void authSetupHandler() {
  currentRequest = "authsetup";
  debugOut( currentRequest );
  authorisationHandler();
  char activeState[10];
  String result = "";
  result += "<h2>Authorization Setup</h2>\n";
  result += "<div>";
  result += "  <p>Authentification settings, configure Basic Auth and set a combination of username and password or use the Token Auth and place a Token.</p>";
  result += "</div>\n";
  result += "<form action='/authform' method='POST' id='authsetup'>\n";
  result += "<table style='border:none;'>\n";
  result += "<tbody>\n";
  result += "  <tr><td><strong>Basic Authentication</strong></td><td></td></tr>\n"; // head line
  strcpy( activeState, ( authentication ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='authentication'>Basic Auth: </label></td>\n";
  result += "    <td><input id='authentication' name='authentication' type='checkbox' onclick='renderRows( this, \"authRow\" )' " + String( activeState ) + " /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='authRow'>\n";
  result += "    <td><label for='authuser'>Username: </label></td>\n";
  result += "    <td><input id='authuser' name='authuser' type='text' placeholder='Username' value='" + String( authuser ) + "' size='" + String( eepromStringSize ) + "' /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='authRow'>\n";
  result += "    <td><label for='authpass'>User Password: </label></td>\n";
  result += "    <td><input id='authpass' name='authpass' type='password' placeholder='Password' value='" + String( authpass ) + "' size='" + String( eepromStringSize ) + "' />\n";
  result += "    <button name='showpw' type='button' onclick='toggleVisibility( \"authpass\" )'>&#128065;</button></td>\n";
  result += "  </tr>\n";
  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line
  result += "  <tr><td><strong>Token Authentication</strong></td><td></td></tr>\n"; // head line
  strcpy( activeState, ( tokenauth ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='tokenauth'>Token Auth: </label></td>\n";
  result += "    <td><input id='tokenauth' name='tokenauth' type='checkbox' onclick='renderRows( this, \"tokenAuthRow\" )' " + String( activeState ) + " /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='tokenAuthRow'>\n";
  result += "    <td><label for='token'>Token: </label></td>\n";
  result += "    <td>\n";
  result += "      <input id='token' name='token' type='password' placeholder='' value='" + String( token ) + "' size='" + String( eepromStringSize ) + "' /> \n";
  result += "      <button name='showtoken' type='button' onclick='toggleVisibility( \"token\" )'>&#128065;</button>";
  result += "<button name='genToken' type='button' onclick='generateToken( \"token\" )'>Generate Token</button>\n";
  result += "    </td>\n";
  result += "  </tr>\n";
  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line
  result += "  <tr><td><strong>Disable Authentification after Boot</strong></td><td></td></tr>\n"; // head line
  strcpy( activeState, ( secpush ? "checked" : "" ) );
  result += "  <tr>\n";
  result += "    <td><label for='secpush'>SecPush: </label></td>\n";
  result += "    <td><input id='secpush' name='secpush' type='checkbox' onclick='renderRows( this, \"secPushRow\" )' " + String( activeState ) + " /></td>\n";
  result += "  </tr>\n";
  result += "  <tr class='secPushRow'>\n";
  result += "    <td><label for='secpushtime'>SecPush Time (in Seconds): </label></td>\n";
  result += "    <td><input id='secpushtime' name='secpushtime' type='number' placeholder='300' value='" + String( secpushtime ) + "' max='3600' /></td>\n"; // 3600Sec = 1h
  result += "  </tr>\n";
  result += "  <tr><td>&nbsp;</td><td></td></tr>\n"; // dummy line
  result += "  <tr>\n";
  result += "    <td></td><td><button name='submit' type='submit'>Submit</button></td>\n";
  result += "  </tr>\n";
  result += "</tbody>\n";
  result += "</table>\n";
  result += "</form>\n";
  result += "<a href='/'>Zur&uuml;ck</a>\n";
  result = htmlBody( result );
  response( result, "text/html" );
}

// Auth Form Handler
void authFormHandler() {
  currentRequest = "authform";
  debugOut( currentRequest );
  authorisationHandler();
  // Authentication stuff
  authentication = server.arg("authentication") == "on" ? true : false;
  String authuserString = server.arg("authuser");
  authuserString.toCharArray(authuser, eepromStringSize);
  String authpassString = server.arg("authpass");
  authpassString.toCharArray(authpass, eepromStringSize);
  // Token based Authentication stuff
  tokenauth = server.arg("tokenauth") == "on" ? true : false;
  String tokenString = server.arg("token");
  tokenString.toCharArray(token, eepromStringSize);
  secpush = server.arg("secpush") == "on" ? true : false;
  String secpushtimeString = server.arg("secpushtime");
  secpushtime = secpushtimeString.toInt();
// save data into eeprom
  bool result = saveSettings();
  delay(50);
  // send user to restart page
  String location = "/restart";
  if( tokenauth ) { location += "?apikey=" + String( token ); }
  server.sendHeader( "Location",location );
  server.send( 303 );
}

// Restart Handler
void restartHandler() {
  currentRequest = "restart";
  debugOut( currentRequest );
  authorisationHandler();
  String webString = "<html>\n";
  webString += "<head>\n";
  webString += "  <meta http-equiv='Refresh' content='4; url=/' />\n";
  webString += spinnerJS();
  webString += spinnerCSS();
  webString += "</head>\n";
  webString += "<body style=\"text-align:center;\">\n";
  webString += "  <div id='spinner_wrap'><span id='spinner'>Loading...</span></div>\n";
  webString += "</body>\n";
  webString += "</html>\n";
  response( webString, "text/html" );
  if( server.hasArg( "reset" ) ) {
    // clear eeprom
    for (int i = 0 ; i < EEPROM.length() ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
  }
  delay(50);
  ESP.restart();
}

// Reset Handler
void resetHandler() {
  currentRequest = "reset";
  debugOut( currentRequest );
  authorisationHandler();
  String webString = "<html>\n";
  webString += "<head>\n";
  webString += "  <meta http-equiv='Refresh' content='4; url=/' />\n";
  webString += spinnerJS();
  webString += spinnerCSS();
  webString += "</head>\n";
  webString += "<body style=\"text-align:center;\">\n";
  webString += "  <div id='spinner_wrap'><span id='spinner'>Loading...</span></div>\n";
  webString += "</body>\n";
  webString += "</html>\n";
  response( webString, "text/html" );
  delay(50);
  ESP.reset();
}

// Metrics Handler
void metricsHandler() {
  currentRequest = "metrics";
  debugOut( currentRequest );
  authorisationHandler();
  unsigned long currentMillis = millis();
  String checksumString = sha1( String( ssid ) + String( password ) + String( dnsname ) + String( place ) 
    + String( (int) authentication ) + String( authuser ) + String( authpass )
    + String( (int) tokenauth ) + String( token ) 
    + String( (int) secpush ) + String( secpushtime ) 
    + String( (int) silent ) + String( (int) debug ) 
    + String( (int) staticIP ) + ip2Str( ipaddr ) + ip2Str( gateway ) + ip2Str( subnet ) + ip2Str( dns1 ) + ip2Str( dns2 )
    );
  char checksum[ eepromchk ];
  checksumString.toCharArray(checksum, eepromchk); // write checksumString (Sting) into checksum (char Arr)
  webString = "";
  // Info and Voltage 
  webString += "# HELP esp_firmware_build_info A metric with a constant '1' value labeled by version, board type, dhttype and dhtpin\n";
  webString += "# TYPE esp_firmware_build_info gauge\n";
  webString += "esp_firmware_build_info{";
  webString += "version=\"" + String( history ) + "\"";
  webString += ",board=\"" + String( ARDUINO_BOARD ) + "\"";
  webString += ",nodename=\"" + String( dnsname ) + "\"";
  webString += ",nodeplace=\"" + String( place ) + "\"";
  webString += "} 1\n";
  // Authentification
  bool authEnabled = ( authentication || tokenauth ) ? true : false;
  String authType = ( authentication ) ? "BasicAuth" : "";
  if( authentication && tokenauth ) authType += " & ";
  authType += ( tokenauth ) ? "TokenAuth" : "";
  webString += "# HELP esp_device_auth En or Disabled authentification\n";
  webString += "# TYPE esp_device_auth gauge\n";
  webString += "esp_device_auth{";
  webString += "authtype=\"" + authType + "\"";
  webString += "} " + String( (int) authEnabled ) + "\n";
  // SecPush
  webString += "# HELP esp_device_secpush 1 or 0 En or Disabled SecPush\n";
  webString += "# TYPE esp_device_secpush gauge\n";
  webString += "esp_device_secpush{";
  webString += "secpushstate=\"" + String( (int) secpushstate ) + "\"";
  webString += ",secpushtime=\"" + String( (int) secpushtime ) + "\"";
  webString += "} " + String( (int) secpush ) + "\n";
  // WiFi RSSI 
  String wifiRSSI = String( WiFi.RSSI() );
  webString += "# HELP esp_wifi_rssi Wifi Signal Level in dBm\n";
  webString += "# TYPE esp_wifi_rssi gauge\n";
  webString += "esp_wifi_rssi " + wifiRSSI.substring( 1 ) + "\n";
  // SRAM
  webString += "# HELP esp_device_sram SRAM State\n";
  webString += "# TYPE esp_device_sram gauge\n";
  webString += "esp_device_sram{";
  webString += "size=\"" + String( ESP.getFlashChipRealSize() ) + "\"";
  webString += ",id=\"" + String( ESP.getFlashChipId() ) + "\"";
  webString += ",speed=\"" + String( ESP.getFlashChipSpeed() ) + "\"";
  webString += ",mode=\"" + String( ESP.getFlashChipMode() ) + "\"";
  webString += "} " + String( ESP.getFlashChipSize() ) + "\n";
  // DHCP or static IP
  webString += "# HELP esp_device_dhcp Network configured by DHCP\n";
  webString += "# TYPE esp_device_dhcp gauge\n";
  //// ip in info
  webString += "esp_device_dhcp ";
  webString += staticIP ? "0" : "1";
  webString += "\n";
  // silent mode
  webString += "# HELP esp_device_silent Silent Mode enabled = 1 or disabled = 0\n";
  webString += "# TYPE esp_device_silent gauge\n";
  webString += "esp_device_silent ";
  webString += silent ? "1" : "0";
  webString += "\n";
  // debug mode
  webString += "# HELP esp_device_debug Debug Mode enabled = 1 or disabled = 0\n";
  webString += "# TYPE esp_device_debug gauge\n";
  webString += "esp_device_debug ";
  webString += debug ? "1" : "0";
  webString += "\n";
  // eeprom info
  webString += "# HELP esp_device_eeprom_size Size of EEPROM in byte\n";
  webString += "# TYPE esp_device_eeprom_size gauge\n";
  webString += "esp_device_eeprom_size " + String( eepromSize ) + "\n";
  webString += "# HELP esp_device_config_size Size of EEPROM Configuration data in byte\n";
  webString += "# TYPE esp_device_config_size counter\n";
  webString += "esp_device_config_size " + String( configSize ) + "\n";
  webString += "# HELP esp_device_eeprom_free Size of available/free EEPROM in byte\n";
  webString += "# TYPE esp_device_eeprom_free gauge\n"; // 284
  webString += "esp_device_eeprom_free " + String( ( eepromSize - configSize ) ) + "\n";
  // device uptime
  webString += "# HELP esp_device_uptime Uptime of the Device in Secondes \n";
  webString += "# TYPE esp_device_uptime counter\n";
  webString += "esp_device_uptime ";
  webString += millis() / 1000;
  webString += "\n";

  webString += "# HELP esp_ampel_state Uptime of the Device in Secondes \n";
  webString += "# TYPE esp_ampel_state counter\n";
  webString += "esp_ampel_state " + String( state ) + "\n";

  // checksum
  webString += "# HELP esp_mode_checksum Checksum validation: 1 = valid, 0 = invalid (need to run Setup again)\n";
  webString += "# TYPE esp_mode_checksum gauge\n";
  webString += "esp_mode_checksum ";
  if( strcmp( checksum, lastcheck ) == 0 ) {
    webString += "1";
  } else {
    webString += "0";
  }
  webString += "\n";
  // Voltage depending on what sensors are in use (Pin A0?)
  webString += "# HELP esp_voltage current Voltage metric\n";
  webString += "# TYPE esp_voltage gauge\n";
  webString += "esp_voltage " + String( (float) ESP.getVcc() / 1024.0 / 10.0 ) + "\n";

  // Total Runtime
  long TOTALRuntime = millis() - currentMillis;
  webString += "# HELP esp_total_runtime in milli Seconds to collect data from all Sensors\n";
  webString += "# TYPE esp_total_runtime gauge\n";
  webString += "esp_total_runtime " + String( TOTALRuntime ) + "\n";
  // End of data, response the request
  response( webString, "text/plain" );
}


// End helper functions
//


//
// Setup
//

void setup() {

  // Set Relais Pins
  pinMode( r, OUTPUT );
  pinMode( y, OUTPUT );
  pinMode( g, OUTPUT );

  digitalWrite( r, HIGH );
  digitalWrite( y, HIGH );
  digitalWrite( g, HIGH );
    
  if( ! silent ) { 
    // Initialize the LED_BUILTIN pin as an output
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Turn the LED off by HIGH!
  }

  // Start eeprom 
  EEPROM.begin( eepromSize );
  // load data from eeprom
  bool loadingSetup = loadSettings(); 

  if( debug ) {
    Serial.begin(115200);
    while( ! Serial );
    Serial.println( "" );
    Serial.println( ",----------------------." );
    Serial.println( "| esp8266 node startup |" );
    Serial.println( "`----------------------´" );
    Serial.print( "Starting Device: '" );
    Serial.print( dnsname );
    Serial.println( "'" );
  }
  
  // start Wifi mode depending on Load Config (no valid config = start in AP Mode)
  if( loadingSetup ) {

    if( debug ) { Serial.println( "Starting WiFi Client Mode" ); }
    
    // use a static IP?
    if( staticIP ) {
      if( debug ) { Serial.print( "Using static IP: " ); Serial.println( ip ); }
      WiFi.config( ipaddr, gateway, subnet, dns1, dns2 ); // static ip adress
    }
    // Setup Wifi mode
    // WIFI_AP = Wifi Accesspoint
    // WIFI_STA = Wifi Client
    // WIFI_AP_STA = Wifi Accesspoint Client (Mesh)
    // WIFI_OFF = Wifi off
    WiFi.mode( WIFI_STA );
    // set dnshostname
    WiFi.hostname( dnsname );
    // wifi connect to AP
    if( debug ) { Serial.print( "Connect to AP " + String( ssid ) +  ": " ); }
    WiFi.begin( ssid, password );
    while( WiFi.status() != WL_CONNECTED ) {
      if( !silent ) {
        digitalWrite(LED_BUILTIN, LOW);
        delay( 200 );
        digitalWrite(LED_BUILTIN, HIGH);
      }
      delay( 200 );
      if( debug ) { Serial.print( "." ); }
    }
//    delay(200);
    if( debug ) { Serial.println( " ok" ); }
    // get the local ip adress
    ip = WiFi.localIP().toString();
    SoftAccOK = false;
    captiveCall = false;
    if( debug ) { Serial.print( "Local Device IP: " ); Serial.println( ip ); }

  } else {

    if( debug ) { Serial.print( "Starting in AP Mode: " ); }
    
    // Setup Wifi mode
    WiFi.mode( WIFI_AP );

    // set dnshostname
    WiFi.hostname( dnsname );
    
    // Setup AP Config
    IPAddress lip(192,168,4,1);
    IPAddress lgw(192,168,4,1);
    IPAddress lsub(255,255,255,0);
    WiFi.softAPConfig( lip, lgw, lsub );
    delay( 50 );
    
    // start in AP mode, without Password
    String appString = String( dnsname ) + "_setup";
    char apname[ eepromStringSize ];
    appString.toCharArray(apname, eepromStringSize);
    SoftAccOK = WiFi.softAP( apname, "" );
    delay( 50 );
    if( SoftAccOK ) {
//    while( WiFi.status() != WL_CONNECTED ) {
      digitalWrite(LED_BUILTIN, LOW);
      delay( 200 );
      digitalWrite(LED_BUILTIN, HIGH);
      delay( 300);
      digitalWrite(LED_BUILTIN, LOW);
      delay( 200 );
      digitalWrite(LED_BUILTIN, HIGH);
      delay( 300);
      digitalWrite(LED_BUILTIN, LOW);
      delay( 200 );
      digitalWrite(LED_BUILTIN, HIGH);
      if( debug ) { Serial.println( "ok" ); }
    } else {
      if( debug ) { Serial.println( "failed" ); }
    }
    //delay(500);
    ip = WiFi.softAPIP().toString();
    
    /* Setup the DNS server redirecting all the domains to the apIP */
    // https://github.com/tzapu/WiFiManager
    dnsServer.setErrorReplyCode( DNSReplyCode::NoError );
    bool dnsStart = dnsServer.start( DNS_PORT, "*", WiFi.softAPIP() );
    if( debug ) {
      Serial.print( "starting dns server for captive Portal: " );
      if( dnsStart ) {
        Serial.println( "ok" );
      } else {
        Serial.println( "failed" );
      }
    }
    delay(50);

  }

  // activate initial secpush
  if( secpush ) secpushstate = true;
  
  // 404 Page
  server.onNotFound( notFoundHandler );

  // default, main page
  server.on( "/", handle_root );

  // ios captive portal detection request
  server.on( "/hotspot-detect.html", captiveHandler );

  // android captive portal detection request
  server.on( "/generate_204", captiveHandler );
  
  // simple wifi signal strength
  server.on( "/signal", HTTP_GET, signalHandler );

  // Network setup page & form target
  server.on( "/network", HTTP_GET, networkSetupHandler );
  server.on( "/networkform", HTTP_POST, networkFormHandler );

  // Device setup page & form target
  server.on( "/device", HTTP_GET, deviceSetupHandler );
  server.on( "/deviceform", HTTP_POST, deviceFormHandler );

  // Authentification page & form target
  server.on( "/auth", HTTP_GET, authSetupHandler );
  server.on( "/authform", HTTP_POST, authFormHandler );

  // restart page
  server.on( "/restart", HTTP_GET, restartHandler );

  // reset page
  server.on( "/reset", HTTP_GET, resetHandler );

  // metrics (simple prometheus ready metrics output)
  server.on( "/metrics", HTTP_GET, metricsHandler );

  // Ampel function
  server.on( "/ampel", HTTP_GET, ampelHandler );

//  // Example Raw Response
//  server.on( "/url", HTTP_GET, []() {
//    response( htmlBody( result ), "text/html");
//  });

  // Starting the Weberver
  if( debug ) Serial.print( "Starting the Web-Server: " );
  server.begin();
  if( debug ) { Serial.println( "done" ); Serial.println( "waiting for client connections" ); }

  // set initial state
  ampelSet();

} // end setup

//
// main loop (handle http requests)
//

void loop() {

  // DNS redirect clients
  if( SoftAccOK ) {
    dnsServer.processNextRequest();
  }

  // handle http requests
  server.handleClient();

  if( secpushstate ) secpushstate = securePush();

}

// End Of File
