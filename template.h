#ifndef HTMLTEMPLATE
#define HTMLTEMPLATE

const char *htmlTemplate = R"delimiter(
<!DOCTYPE HTML>
<html>

<head>
  <title>Underfloor heating</title>
  <meta name="viewport"
    content="width=device-width, user-scalable=no, minimum-scale=1, maximum-scale=1,initial-scale=1" />
  <meta http-equiv='Content-Type' content='text/html; charset=utf-8' />
  <style>
    body {
      background-color: limegreen;
      font-family: sans-serif;
    }

    tr {
      vertical-align: center;
    }

    input {
      color: white;
      font-weight: bold;
      background-color: transparent;
      border: 0pt;
      text-align: right;
      width: 150pt;
    }

    .temperature {
      border: 1px solid yellow;
      border-radius: 4px;
      padding: 6px;
      margin:10px;
    }
    .temperature input {
      font-size: 60pt;
    }

    .temperature span {
      font-size: 32pt;
      color: white;
      font-weight: bold;
      vertical-align:baseline;
    }

    #vacation {
      font-size: 18pt;
      border: 1px solid blue;
    }
    #vacateLabel {
      text-align: right;
    }

    #submit {
      border: 2pt;
      border-radius: 3pt;
      background-color: yellow;
      color: blue;
      width: auto;
    }

    #submit:hover {
      background-color: orange;
    }

    #clear {
      color: darkred
    }

    #clear:hover {
      color: red
    }
  </style>
</head>

<body>
  <h1>Underfloor heating</h1>
  <h2 style="color:yellow">{%serviceState}</h2>
  <table>
    <tr>
      <td title='Adjust the target temperature' class='temperature' id="targetBox">Target<br/>
        <input type='number' id='target' name='target' value='{%target}' step='0.1' min='5.0' max='30.0' /><span
            >C</span></td>
      <td title='Current temperature' class='temperature' id="currentBox">Current<br/>
        <input type='number' id='current' name='current'  value='{%current}' readonly /><span
          >C</span></td>


    </tr>
    <tr style="display:none">
      <td title='Adjust this over many days until the temperature is correct. Then leave it be.'>Factor</td>
      <td><input type='number' id='factor' name='factor' min='0.5' step='0.01' max='1.2' value='{%factor}' /></td>
    </tr>
    <tr>
      <td id="vacateLabel" title='Heating will be minimal until this date. For normal operation, clear it or set to a past date.'>
        Vacate until</td>
      <td><input type='date' min='2018-01-01' id='vacation' name='vacation'
          value='{%vacation}' />&nbsp;&nbsp;<span id='clear' title="Turn off vacation"
          onClick="clearDate()">X</span></td>
    </tr>
    <tr>
      <td>&nbsp;</td>
    </tr>
    <tr>
      <td></td>
      <td style="text-align: right;"><input id='submit' type='button' value='Update' onClick='sendUpdate()' /></td>
    </tr>
  </table> <!-- <input type='text' onChange="g('ans').innerHTML=p(this.value);"/><span id='ans'></span> -->
  <hr /> <a href="/upd">More parameters</a> &nbsp;&nbsp;<a href="/log">Log</a>&nbsp;&nbsp;Service: <a
    href="/service?set=on">ON</a> <a href="/service?set=off">OFF</a> <a href="/service?set=run">Normal</a>
  <p>&nbsp;</p>
  <div style="display:none">
    <form id='dataForm' method='POST' action='/'><textarea id='data' name='data'>{%params}</textarea></form>
  </div>
  <script>
    function g(name) { return document.getElementById(name); }
    function clearDate() { g("vacation").value = ""; }
    function p(name) { return dataObject[name]; }
    function sendUpdate() {
      var s = '{"target":' + g('target').value + ',"vacation":"'
        + g('vacation').value + '","factor":' + g('factor').value + '}'; g('data').value = s; g('dataForm').submit();
    }
    var dataObject = JSON.parse(g('data').value);
    var y = (new Date()).getFullYear();
    factor.value = p("factor");
    target.value = p("target");
    var vacationVal = dataObject["vacation"];
    if (vacationVal.length > 0 && vacationVal.length < 10) { vacationVal = "20" + vacationVal; }
    vacation.max = "" + (y + 1) + "-12-31"; 
    vacation.min = "" + y + "-01-01"; 
    vacation.value = vacationVal;
  </script>
</body>

</html>
)delimiter";

#endif
