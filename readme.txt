sama5_boot overview :
 
1. This project is based on at91bootstrap .
  1.1 hardware is MYB-SAMA5D3X 
  1.2 use windows gcc / linux gcc 
2. support multi-boot  
  1.1 nor flash boot 
  1.2 nand flash boot 
2. jump to uboot  

How to do it ?
1. You should download Cygwin on windows, Then Change your directory in your workspace.
$cd source/
$make mrproper 

The make mrproper will clean config file and many obj files in your workspace. 

 
