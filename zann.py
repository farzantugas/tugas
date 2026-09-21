print("================================")
print("=========Sistem Nilai===========")
print("================================")
nama_siswa = input("Masukan nama siswa: ")
nilai_ujian = int(input("Masukan nilai ujian (0-100): "))

if nilai_ujian >= 90:
    print("Nama siswa:", nama_siswa)
    print("status: Lulus")
    print("Predikat: A(Sangat Memuaskan)")
elif nilai_ujian >= 80:
     print("Nama siswa:", nama_siswa)
     print("status: Lulus")
     print("Predikat: B(Baik)")
elif nilai_ujian >= 75:
     print("Nama siswa:", nama_siswa)
     print("status: Lulus")
     print("Predikat: C(Cukup)")
elif nilai_ujian < 75:
     print("Nama siswa:", nama_siswa)
     print("status: tidak")
     print("Predikat: D(Tidak Lulus/Remedial)")
else:
     print("Pilihan tidak valid, Silahkan masukan nilai ujian antara 0-100")
  
