<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Document</title>
</head>
<body>
<?php
    include "service/database.php"
    if(isset($_POST['login'])) {
        $username = $_POST['username'];
        $password = $_POST['password'];
    }


?>
<h3>HALAMAN LOGIN</h3>
<form action="login.php" method="POST">
    <input type="text" name="username" placeholder="username" id="">
    <input type="password" name="password" placeholde="password" id="">
    <button type="submit" name="login">MASUK</button>
</form>
</body>
</html>